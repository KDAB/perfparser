/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "perfsymboltable.h"
#include "perfunwind.h"
#include "perfdwarfdiecache.h"
#include "perfeucompat.h"

#include <QDebug>
#include <QDir>
#include <QScopeGuard>
#include <QStack>

#include <dwarf.h>
#include <elfutils/libdwelf.h>

#if HAVE_DWFL_GET_DEBUGINFOD_CLIENT
#include <debuginfod.h>
#endif

PerfSymbolTable::PerfSymbolTable(qint32 pid, Dwfl_Callbacks *callbacks, PerfUnwind *parent) :
    m_perfMapFile(QDir::tempPath() + QDir::separator()
                  + QLatin1String("perf-%1.map").arg(QString::number(pid))),
    m_hasPerfMap(m_perfMapFile.exists()),
    m_cacheIsDirty(false),
    m_unwind(parent),
    m_callbacks(callbacks),
    m_pid(pid)
{
    QObject::connect(&m_elfs, &PerfElfMap::aboutToInvalidate,
                     &m_elfs, [this](const PerfElfMap::ElfInfo &elf) {
                        if (m_dwfl && !m_cacheIsDirty && dwfl_addrmodule(m_dwfl, elf.addr)) {
                            m_cacheIsDirty = true;
                        }
                     });

    m_dwfl = dwfl_begin(m_callbacks);

#if HAVE_DWFL_GET_DEBUGINFOD_CLIENT
    auto client = dwfl_get_debuginfod_client(m_dwfl);
    debuginfod_set_user_data(client, this);
    debuginfod_set_progressfn(client, [](debuginfod_client* client, long numerator, long denominator) {
        auto self = reinterpret_cast<PerfSymbolTable*>(debuginfod_get_user_data(client));
        auto url = self->m_unwind->resolveString(QByteArray(debuginfod_get_url(client)));
        self->m_unwind->sendDebugInfoDownloadProgress(url, numerator, denominator);
        // NOTE: eventually we could add a back channel to allow the user to cancel an ongoing download
        //       to do so, we'd have to return any non-zero value here then
        return 0;
    });
#endif

    dwfl_report_begin(m_dwfl);

    // "DWFL can not be used until this function returns 0"
    const int reportEnd = dwfl_report_end(m_dwfl, NULL, NULL);
    Q_ASSERT(reportEnd == 0);
}

PerfSymbolTable::~PerfSymbolTable()
{
    dwfl_end(m_dwfl);
}

static bool findInExtraPath(QFileInfo &path, const QString &fileName)
{
    path.setFile(path.absoluteFilePath() + QDir::separator() + fileName);
    if (path.isFile())
        return true;

    const QDir absDir = path.absoluteDir();
    const auto entries = absDir.entryList({}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        path.setFile(absDir, entry);
        if (findInExtraPath(path, fileName))
            return true;
    }
    return false;
}

static bool findBuildIdPath(QFileInfo &path, const QString &fileName)
{
    path.setFile(path.absoluteFilePath() + QDir::separator() + fileName);
    if (path.isFile())
        return true;

    path.setFile(path.absoluteFilePath() + QDir::separator() + QLatin1String("elf"));
    if (path.isFile())
        return true;

    return false;
}

static QStringList splitPath(const QString &path)
{
    return path.split(QDir::listSeparator(), Qt::SkipEmptyParts);
}

static bool matchesBuildId(const QByteArray &buildId, const QFileInfo& path)
{
    if (buildId.isEmpty())
        return true;

    QFile file(path.absoluteFilePath());
    file.open(QIODevice::ReadOnly);
    if (!file.isOpen())
        return false;

    auto elf = elf_begin(file.handle(), ELF_C_READ, NULL);
    auto guard = qScopeGuard([elf] { elf_end(elf); });

    if (!elf)
        return false;

    const void* pathBuildId = nullptr;
    auto len = dwelf_elf_gnu_build_id(elf, &pathBuildId);

    if (len != buildId.size())
        return false;

    return memcmp(buildId.constData(), pathBuildId, len) == 0;
}

QFileInfo PerfSymbolTable::findFile(const QString& path, const QString &fileName,
                                    const QByteArray &buildId) const
{
    QFileInfo fullPath;
    // first try to find the debug information via build id, if available
    if (!buildId.isEmpty()) {
        const QString buildIdPath = path + QDir::separator() + QString::fromUtf8(buildId.toHex());
        const auto extraPaths = splitPath(m_unwind->debugPath());
        for (const QString &extraPath : extraPaths) {
            fullPath.setFile(extraPath);
            if (findBuildIdPath(fullPath, buildIdPath))
                return fullPath;
        }
    }

    if (!m_unwind->appPath().isEmpty()) {
        // try to find the file in the app path
        fullPath.setFile(m_unwind->appPath());
        if (findInExtraPath(fullPath, fileName) && matchesBuildId(buildId, fullPath)) {
            return fullPath;
        }
    }

    // try to find the file in the extra libs path
    const auto extraPaths = splitPath(m_unwind->extraLibsPath());
    for (const QString &extraPath : extraPaths) {
        fullPath.setFile(extraPath);
        if (findInExtraPath(fullPath, fileName) && matchesBuildId(buildId, fullPath)) {
            return fullPath;
        }
    }

    // last fall-back, try the system root
    fullPath.setFile(m_unwind->systemRoot() + path);
    return fullPath;
}

void PerfSymbolTable::registerElf(const PerfRecordMmap &mmap, const QByteArray &buildId)
{
    const auto filePath = QString::fromUtf8(mmap.filename());
    // special regions, such as [heap], [vdso], [stack], [kernel.kallsyms]_text ... as well as //anon
    const bool isSpecialRegion = (filePath.startsWith(QLatin1Char('[')) && filePath.contains(QLatin1Char(']')))
        || filePath.startsWith(QLatin1String("/dev/")) || filePath.startsWith(QLatin1String("/memfd:"))
        || filePath.startsWith(QLatin1String("/SYSV")) || filePath == QLatin1String("//anon");
    const auto fileName = isSpecialRegion ? QString() : QFileInfo(filePath).fileName();
    QFileInfo fullPath;
    if (isSpecialRegion) {
        // don not set fullPath, these regions don't represent a real file
    } else if (mmap.pid() != PerfUnwind::s_kernelPid) {
        fullPath = findFile(filePath, fileName, buildId);

        if (!fullPath.isFile()) {
            m_unwind->sendError(PerfUnwind::MissingElfFile,
                                PerfUnwind::tr("Could not find ELF file for %1. "
                                               "This can break stack unwinding "
                                               "and lead to missing symbols.").arg(filePath));
        } else {
            ElfAndFile elf(fullPath);
            if (!elf.elf())
                fullPath = QFileInfo();
            else if (!m_firstElf.elf())
                m_firstElf = std::move(elf);
        }
    } else { // kernel
        fullPath.setFile(m_unwind->systemRoot() + filePath);
        ElfAndFile elf(fullPath);
        if (!elf.elf())
            fullPath = QFileInfo();
    }

    m_elfs.registerElf(mmap.addr(), mmap.len(), mmap.pgoff(), fullPath,
                       fileName.toUtf8(), mmap.filename());

    // There is no need to clear the symbol or location caches in PerfUnwind. Some locations become
    // stale this way, but we still need to keep their IDs, as the receiver might still use them for
    // past frames.
    if (m_cacheIsDirty)
        clearCache();
}

int PerfSymbolTable::insertSubprogram(CuDieRangeMapping *cudie, Dwarf_Die *top, Dwarf_Addr entry,
                                      quint64 offset, quint64 size, quint64 relAddr,
                                      qint32 binaryId, qint32 binaryPathId, qint32 actualPathId,
                                      qint32 inlineCallLocationId, bool isKernel)
{
    int line = 0;
    dwarf_decl_line(top, &line);
    int column = 0;
    dwarf_decl_column(top, &column);
    const QByteArray file = absoluteSourcePath(dwarf_decl_file(top), cudie->cudie());

    qint32 fileId = m_unwind->resolveString(file);
    int locationId = m_unwind->resolveLocation(PerfUnwind::Location(entry, relAddr, fileId, m_pid, line,
                                                                    column, inlineCallLocationId));
    qint32 symId = m_unwind->resolveString(cudie->dieName(top));
    m_unwind->resolveSymbol(locationId, PerfUnwind::Symbol{symId, offset, size, binaryId, binaryPathId, actualPathId, isKernel});

    return locationId;
}

int PerfSymbolTable::parseDie(CuDieRangeMapping *cudie, Dwarf_Die *top, quint64 offset, quint64 size, quint64 relAddr, qint32 binaryId, qint32 binaryPathId, qint32 actualPathId,
                              bool isKernel, Dwarf_Files *files, Dwarf_Addr entry, qint32 parentLocationId)
{
    int tag = dwarf_tag(top);
    switch (tag) {
    case DW_TAG_inlined_subroutine: {
        PerfUnwind::Location location(entry);
        Dwarf_Attribute attr;
        Dwarf_Word val = 0;
        const QByteArray file = absoluteSourcePath((dwarf_formudata(dwarf_attr(top, DW_AT_call_file, &attr), &val) == 0)
                                                       ? dwarf_filesrc(files, val, nullptr, nullptr)
                                                       : "",
                                                   cudie->cudie());
        location.file = m_unwind->resolveString(file);
        location.line
                = (dwarf_formudata(dwarf_attr(top, DW_AT_call_line, &attr), &val) == 0)
                ? static_cast<qint32>(val) : -1;
        location.column
                = (dwarf_formudata(dwarf_attr(top, DW_AT_call_column, &attr), &val) == 0)
                ? static_cast<qint32>(val) : -1;
        location.pid = m_pid;

        location.parentLocationId = parentLocationId;

        int callLocationId = m_unwind->resolveLocation(location);
        return insertSubprogram(cudie, top, entry, offset, size, relAddr, binaryId, binaryPathId, actualPathId, callLocationId, isKernel);
    }
    case DW_TAG_subprogram:
        return insertSubprogram(cudie, top, entry, offset, size, relAddr, binaryId, binaryPathId, actualPathId, -1, isKernel);
    default:
        return -1;
    }
}

qint32 PerfSymbolTable::parseDwarf(CuDieRangeMapping *cudie, SubProgramDie *subprogram, const QVector<Dwarf_Die> &inlined,
                                   Dwarf_Addr bias, quint64 offset, quint64 size, quint64 relAddr, qint32 binaryId, qint32 binaryPathId, qint32 actualPathId, bool isKernel)
{
    Dwarf_Files *files = nullptr;
    dwarf_getsrcfiles(cudie->cudie(), &files, nullptr);

    qint32 parentLocationId = -1;
    auto handleDie = [&](Dwarf_Die scope) {
        Dwarf_Addr scopeAddr = bias;
        Dwarf_Addr entry = 0;
        if (dwarf_entrypc(&scope, &entry) == 0 && entry != 0)
            scopeAddr += entry;

        auto locationId = parseDie(cudie, &scope, offset, size, relAddr, binaryId, binaryPathId, actualPathId, isKernel, files, scopeAddr, parentLocationId);
        if (locationId != -1)
            parentLocationId = locationId;
    };

    handleDie(*subprogram->die());
    std::for_each(inlined.begin(), inlined.end(), handleDie);
    return parentLocationId;
}

static void reportError(qint32 pid, const PerfElfMap::ElfInfo& info, const char *message)
{
    qWarning() << "failed to report elf for pid =" << pid << ":" << info << ":" << message;
}

Dwfl_Module *PerfSymbolTable::reportElf(const PerfElfMap::ElfInfo& info)
{
    if (!info.isValid() || !info.isFile())
        return nullptr;

    dwfl_report_begin_add(m_dwfl);
    Dwfl_Module *ret = dwfl_report_elf(
                m_dwfl, info.originalFileName.constData(),
                info.localFile.absoluteFilePath().toLocal8Bit().constData(), -1, info.addr - info.pgoff,
                false);
    if (!ret) {
        reportError(m_pid, info, dwfl_errmsg(dwfl_errno()));
        m_cacheIsDirty = true;
    } else {
        // set symbol table as user data, cf. find_debuginfo callback in perfunwind.cpp
        void** userData;
        Dwarf_Addr start = 0;
        Dwarf_Addr end = 0;

        dwfl_module_info(ret, &userData, &start, &end, nullptr, nullptr, nullptr, nullptr);
        *userData = this;
        m_elfs.updateElf(info.addr, start, end);
    }
    const int reportEnd = dwfl_report_end(m_dwfl, NULL, NULL);
    Q_ASSERT(reportEnd == 0);

    return ret;
}

Dwfl_Module *PerfSymbolTable::module(quint64 addr)
{
    return module(addr, findElf(addr));
}

Dwfl_Module *PerfSymbolTable::module(quint64 addr, const PerfElfMap::ElfInfo &elf)
{
    if (!m_dwfl)
        return nullptr;

    if (elf.hasBaseAddr() && elf.baseAddr != elf.addr) {
        const auto base = m_elfs.findElf(elf.baseAddr);
        if (base.addr == elf.baseAddr && !base.pgoff && elf.originalPath == base.originalPath && elf.addr != base.addr)
            return module(addr, base);
    }

    Dwfl_Module *mod = dwfl_addrmodule(m_dwfl, addr);

    if (!mod && elf.isValid()) {
        // check whether we queried for an address outside the elf range parsed
        // by dwfl. If that is the case, then we would invalidate the cache and
        // re-report the library again - essentially recreating the current state
        // for no gain, except wasting time
        mod = dwfl_addrmodule(m_dwfl, elf.addr - elf.pgoff);
    }

    if (mod) {
        // If dwfl has a module and it's not the same as what we want, report the module
        // we want. Many modules overlap ld.so, so if we've reported even one sample from
        // ld.so we would otherwise be blocked from reporting anything that overlaps it.
        Dwarf_Addr mod_start = 0;
        dwfl_module_info(mod, nullptr, &mod_start, nullptr, nullptr, nullptr, nullptr,
                         nullptr);
        if (elf.addr - elf.pgoff == mod_start)
            return mod;
    }
    return reportElf(elf);
}

QFileInfo PerfSymbolTable::findDebugInfoFile(
        const QString &root, const QString &file,
        const QString &debugLinkString)
{
    auto dir = QFileInfo(root).dir();
    const auto folder = QFileInfo(file).path();

    QFileInfo debugLinkFile;

    // try in .debug folder
    if (!folder.isEmpty()) {
        debugLinkFile.setFile(dir.path() + QDir::separator() + folder + QDir::separator() + QLatin1String(".debug")
                              + QDir::separator() + debugLinkString);
        if (debugLinkFile.isFile())
            return debugLinkFile;
    }

    debugLinkFile.setFile(dir.path() + QDir::separator() + file + QDir::separator() + QLatin1String(".debug")
                          + QDir::separator() + debugLinkString);
    if (debugLinkFile.isFile())
        return debugLinkFile;

    // try again in /usr/lib/debug folder
    // some distros use for example /usr/lib/debug/lib (ubuntu) and some use /usr/lib/debug/usr/lib (fedora)
    const auto usr = QString(QDir::separator() + QLatin1String("usr") + QDir::separator());
    auto folderWithoutUsr = folder;
    folderWithoutUsr.replace(usr, QDir::separator());

    // make sure both (/usr/ and /) are searched
    for (const auto& path : {folderWithoutUsr, QString(usr + folderWithoutUsr)}) {
        debugLinkFile.setFile(dir.path() + QDir::separator() + QLatin1String("usr") + QDir::separator()
                              + QLatin1String("lib") + QDir::separator() + QLatin1String("debug") + QDir::separator()
                              + path + QDir::separator() + debugLinkString);

        if (debugLinkFile.isFile()) {
            return debugLinkFile;
        }
    }

    debugLinkFile.setFile(dir.path() + QDir::separator() + QLatin1String("usr") + QDir::separator()
                          + QLatin1String("lib") + QDir::separator() + QLatin1String("debug") + QDir::separator()
                          + folder + QDir::separator() + debugLinkString);

    if (debugLinkFile.isFile()) {
        return debugLinkFile;
    }

    debugLinkFile.setFile(dir,
                          QLatin1String("usr") + QDir::separator() + QLatin1String("lib") + QDir::separator()
                              + QLatin1String("debug") + QDir::separator() + debugLinkString);

    if (debugLinkFile.isFile()) {
        return debugLinkFile;
    }

    // try the default files
    if (!folder.isEmpty()) {
        debugLinkFile.setFile(dir.path() + QDir::separator() + folder + QDir::separator() + debugLinkString);
        if (debugLinkFile.isFile()) {
            return debugLinkFile;
        }
    }

    debugLinkFile.setFile(dir.path() + QDir::separator() + file + QDir::separator() + debugLinkString);

    return debugLinkFile;
}

int PerfSymbolTable::findDebugInfo(Dwfl_Module *module, const char *moduleName, Dwarf_Addr base,
                                   const char *file, const char *debugLink,
                                   GElf_Word crc, char **debugInfoFilename)
{
    int ret = dwfl_standard_find_debuginfo(module, nullptr, moduleName, base, file,
                                           debugLink, crc, debugInfoFilename);
    if (ret >= 0 || !debugLink || strlen(debugLink) == 0)
        return ret;

    // fall-back, mostly for situations where we loaded a file via it's build-id.
    // search all known paths for the debug link in that case
    const auto debugLinkPath = QString::fromUtf8(debugLink);
    const auto debugLinkString = QFile(debugLinkPath).fileName();
    auto debugLinkFile = findFile(debugLinkPath, debugLinkString);
    if (!debugLinkFile.isFile()) {
        // fall-back to original file path with debug link file name
        const auto &elf = m_elfs.findElf(base);
        const auto &path = QString::fromUtf8(elf.originalPath);
        debugLinkFile = findDebugInfoFile(m_unwind->systemRoot(), path, debugLinkString);
    }

    /// FIXME: find a proper solution to this
    if (!debugLinkFile.isFile() && QByteArray::fromRawData(file, strlen(file)).endsWith("/elf")) {
        // fall-back to original file if it's in a build-id path
        debugLinkFile.setFile(QString::fromUtf8(file));
    }

    if (!debugLinkFile.isFile())
        return ret;

    const auto path = eu_compat_strdup(debugLinkFile.absoluteFilePath().toUtf8().constData());
    // ugh, nasty - we have to return a fd here :-/
    ret = eu_compat_open(path, O_RDONLY | O_BINARY);
    if (ret < 0 ) {
        qWarning() << "Failed to open debug info file" << path;
        eu_compat_free(path);
    } else {
        *debugInfoFilename = path;
    }
    return ret;
}

PerfElfMap::ElfInfo PerfSymbolTable::findElf(quint64 ip) const
{
    return m_elfs.findElf(ip);
}

int symbolIndex(Elf64_Rel rel)
{
    return ELF64_R_SYM(rel.r_info);
}

int symbolIndex(const Elf64_Rela &rel)
{
    return ELF64_R_SYM(rel.r_info);
}

int symbolIndex(Elf32_Rel rel)
{
    return ELF32_R_SYM(rel.r_info);
}

int symbolIndex(Elf32_Rela rel)
{
    return ELF32_R_SYM(rel.r_info);
}

template<typename elf_relocation_t, typename elf_shdr_t>
int findPltSymbolIndex(Elf_Scn *section, const elf_shdr_t *shdr, Dwarf_Addr addr)
{
    if (shdr->sh_entsize != sizeof(elf_relocation_t)) {
        qWarning() << "size mismatch:" << shdr->sh_entsize << sizeof(elf_relocation_t);
        return -1;
    }
    const size_t numEntries = shdr->sh_size / shdr->sh_entsize;
    const auto *data = elf_getdata(section, nullptr);
    const auto *entries = reinterpret_cast<const elf_relocation_t *>(data->d_buf);
    const auto *entriesEnd = entries + numEntries;
    auto it = std::lower_bound(entries, entriesEnd, addr,
                               [](const elf_relocation_t &lhs, Dwarf_Addr addr) {
                                   return lhs.r_offset < addr;
                                });
    if (it == entriesEnd || it->r_offset != addr)
        return -1;
    return symbolIndex(*it);
}

template<typename elf_dyn_t, typename elf_shdr_t>
Elf64_Addr findPltGotAddr(Elf_Scn *section, elf_shdr_t* shdr)
{
    const auto *data = elf_getdata(section, nullptr);
    const size_t numEntries = shdr->sh_size / shdr->sh_entsize;
    const auto *entries = reinterpret_cast<const elf_dyn_t *>(data->d_buf);
    for (size_t i = 0; i < numEntries; ++i) {
        if (entries[i].d_tag == DT_PLTGOT) {
            return entries[i].d_un.d_ptr;
        }
    }
    return 0;
}

const char *findPltSymbol(Elf *elf, int index)
{
    if (!index) // first plt entry is special, skip it
        return nullptr;

    size_t numSections = 0;
    if (elf_getshdrnum(elf, &numSections) != 0)
        return nullptr;

    Elf64_Addr pltGotAddr = 0;
    Elf_Scn *symtab = nullptr;
    for (size_t i = 0; (!pltGotAddr || !symtab) && i < numSections; ++i) {
        auto *section = elf_getscn(elf, i);
        if (const auto *shdr = elf64_getshdr(section)) {
            if (shdr->sh_type == SHT_DYNAMIC)
                pltGotAddr = findPltGotAddr<Elf64_Dyn>(section, shdr);
            else if (shdr->sh_type == SHT_DYNSYM)
                symtab = section;
        } else if (const auto *shdr = elf32_getshdr(section)) {
            if (shdr->sh_type == SHT_DYNAMIC)
                pltGotAddr = findPltGotAddr<Elf32_Dyn>(section, shdr);
            else if (shdr->sh_type == SHT_DYNSYM)
                symtab = section;
        }
    }

    if (!pltGotAddr || !symtab)
        return nullptr;

    Elf64_Addr indexAddr = 0;
    for (size_t i = 0; !indexAddr && i < numSections; ++i) {
        auto *section = elf_getscn(elf, i);
        if (const auto *shdr = elf64_getshdr(section)) {
            if (shdr->sh_addr <= pltGotAddr && pltGotAddr < shdr->sh_addr + shdr->sh_size)
                indexAddr = shdr->sh_addr + (index + 2) * sizeof(Elf64_Addr);
        } else if (const auto *shdr = elf32_getshdr(section)) {
            if (shdr->sh_addr <= pltGotAddr && pltGotAddr < shdr->sh_addr + shdr->sh_size)
                indexAddr = shdr->sh_addr + (index + 2) * sizeof(Elf32_Addr);
        }
    }

    if (!indexAddr)
        return nullptr;

    int symbolIndex = -1;
    for (size_t i = 0; symbolIndex == -1 && i < numSections; ++i) {
        auto section = elf_getscn(elf, i);
        if (const auto *shdr = elf64_getshdr(section)) {
            if (shdr->sh_type == SHT_REL)
                symbolIndex = findPltSymbolIndex<Elf64_Rel>(section, shdr, indexAddr);
            else if (shdr->sh_type == SHT_RELA)
                symbolIndex = findPltSymbolIndex<Elf64_Rela>(section, shdr, indexAddr);
        } else if (const auto *shdr = elf32_getshdr(section)) {
            if (shdr->sh_type == SHT_REL)
                symbolIndex = findPltSymbolIndex<Elf32_Rel>(section, shdr, indexAddr);
            else if (shdr->sh_type == SHT_RELA)
                symbolIndex = findPltSymbolIndex<Elf32_Rela>(section, shdr, indexAddr);
        }
    }

    if (symbolIndex == -1)
        return nullptr;

    const auto *symtabData = elf_getdata(symtab, nullptr)->d_buf;
    if (const auto *shdr = elf64_getshdr(symtab)) {
        const auto *symbols = reinterpret_cast<const Elf64_Sym *>(symtabData);
        if (symbolIndex >= 0 && uint(symbolIndex) < (shdr->sh_size / shdr->sh_entsize))
            return elf_strptr(elf, shdr->sh_link, symbols[symbolIndex].st_name);
    } else if (const auto *shdr = elf32_getshdr(symtab)) {
        const auto *symbols = reinterpret_cast<const Elf32_Sym *>(symtabData);
        if (symbolIndex >= 0 && uint(symbolIndex) < (shdr->sh_size / shdr->sh_entsize))
            return elf_strptr(elf, shdr->sh_link, symbols[symbolIndex].st_name);
    }
    return nullptr;
}

static QByteArray fakeSymbolFromSection(Dwfl_Module *mod, Dwarf_Addr addr)
{
    Dwarf_Addr bias = 0;
    auto elf = dwfl_module_getelf(mod, &bias);
    const auto moduleAddr = addr - bias;
    auto section = dwfl_module_address_section(mod, &addr, &bias);
    if (!elf || !section)
        return {};

    size_t textSectionIndex = 0;
    if (elf_getshdrstrndx(elf, &textSectionIndex) != 0)
        return {};

    size_t nameOffset = 0;
    size_t entsize = 0;
    if (const auto *shdr = elf64_getshdr(section)) {
        nameOffset = shdr->sh_name;
        entsize = shdr->sh_entsize;
    } else if (const auto *shdr = elf32_getshdr(section)) {
        nameOffset = shdr->sh_name;
        entsize = shdr->sh_entsize;
    }

    auto str = elf_strptr(elf, textSectionIndex, nameOffset);
    if (!str || str == QByteArrayLiteral(".text"))
        return {};

    if (str == QByteArrayLiteral(".plt") && entsize > 0) {
        const auto *pltSymbol = findPltSymbol(elf, addr / entsize);
        if (pltSymbol)
            return demangle(pltSymbol) + "@plt";
    }

    // mark other entries by section name, see also:
    // http://www.mail-archive.com/elfutils-devel@sourceware.org/msg00019.html
    QByteArray sym = str;
    sym.prepend('<');
    sym.append('+');
    sym.append(QByteArray::number(quint64(moduleAddr), 16));
    sym.append('>');
    return sym;
}

int PerfSymbolTable::lookupFrame(Dwarf_Addr ip, bool isKernel,
                                 bool *isInterworking)
{
    auto addressCache = m_unwind->addressCache();

    const auto& elf = findElf(ip);
    auto cached = addressCache->find(elf, ip, &m_invalidAddressCache);
    if (cached.isValid()) {
        *isInterworking = cached.isInterworking;
        return cached.locationId;
    }

    qint32 binaryId = -1;
    qint32 binaryPathId = -1;
    qint32 actualPathId = -1;
    quint64 elfStart = 0;
    if (elf.isValid()) {
        binaryId = m_unwind->resolveString(elf.originalFileName);
        binaryPathId = m_unwind->resolveString(elf.originalPath);
        actualPathId = m_unwind->resolveString(elf.localFile.absoluteFilePath().toUtf8());
        elfStart = elf.hasBaseAddr() ? elf.baseAddr : elf.addr;
    }

    Dwfl_Module *mod = module(ip, elf);

    const bool isArmArch = (m_unwind->architecture() == PerfRegisterInfo::ARCH_ARM);
    PerfUnwind::Location addressLocation(PerfAddressCache::symbolAddress(ip, isArmArch), 0, -1, m_pid);
    PerfUnwind::Location functionLocation(addressLocation);

    QByteArray symname;
    GElf_Off off = 0;

    quint64 start = 0;
    quint64 size = 0;
    quint64 relAddr = 0;
    if (mod) {
        if (!addressCache->hasSymbolCache(elf.originalPath)) {
            // cache all symbols in a sorted lookup table and demangle them on-demand
            // note that the symbols within the symtab aren't necessarily sorted,
            // which makes searching repeatedly via dwfl_module_addrinfo potentially very slow
            addressCache->setSymbolCache(elf.originalPath, PerfAddressCache::extractSymbols(mod, elfStart, isArmArch));
        }

        auto cachedAddrInfo = addressCache->findSymbol(elf.originalPath, addressLocation.address - elfStart);
        if (cachedAddrInfo.isValid()) {
            off = addressLocation.address - elfStart - cachedAddrInfo.offset;
            symname = cachedAddrInfo.symname;
            start = cachedAddrInfo.value;
            size = cachedAddrInfo.size;
            relAddr = PerfAddressCache::alignedAddress(start + off, isArmArch);

            Dwarf_Addr bias = 0;
            functionLocation.address -= off; // in case we don't find anything better

            if (!m_cuDieRanges.contains(mod))
                m_cuDieRanges[mod] = PerfDwarfDieCache(mod);

            auto *cudie = m_cuDieRanges[mod].findCuDie(addressLocation.address);
            if (cudie) {
                bias = cudie->bias();
                const auto offset = addressLocation.address - bias;
                if (auto srcloc = findSourceLocation(cudie->cudie(), offset)) {
                    addressLocation.file = m_unwind->resolveString(srcloc.file);
                    addressLocation.line = srcloc.line;
                    addressLocation.column = srcloc.column;
                }

                auto *subprogram = cudie->findSubprogramDie(offset);
                if (subprogram) {
                    const auto scopes = findInlineScopes(subprogram->die(), offset);

                    // setup function location, i.e. entry point of the (inlined) frame
                    [&](Dwarf_Die die) {
                        Dwarf_Addr entry = 0;
                        dwarf_entrypc(&die, &entry);
                        symname = cudie->dieName(&die); // use name of inlined function as symbol
                        functionLocation.address = entry + bias;
                        functionLocation.file =
                            m_unwind->resolveString(absoluteSourcePath(dwarf_decl_file(&die), cudie->cudie()));
                        dwarf_decl_line(&die, &functionLocation.line);
                        dwarf_decl_column(&die, &functionLocation.column);
                    }(scopes.isEmpty() ? *subprogram->die() : scopes.last());

                    // check if the inline chain was cached already
                    addressLocation.parentLocationId = m_unwind->lookupLocation(functionLocation);
                    // otherwise resolve the inline chain if possible
                    if (!scopes.isEmpty() && !m_unwind->hasSymbol(addressLocation.parentLocationId)) {
                        functionLocation.parentLocationId = parseDwarf(cudie, subprogram, scopes, bias, start, size, relAddr,
                                                                       binaryId, binaryPathId, actualPathId, isKernel);
                    }
                }
            }

            // resolve and cache the inline chain
            if (addressLocation.parentLocationId == -1)
                addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);
        } else {
            // no symbol found
            symname = fakeSymbolFromSection(mod, addressLocation.address);
            addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);
        }

        if (!m_unwind->hasSymbol(addressLocation.parentLocationId)) {
            // no sufficient debug information. Use what we already know
            qint32 symId = m_unwind->resolveString(symname);
            m_unwind->resolveSymbol(addressLocation.parentLocationId,
                                    PerfUnwind::Symbol(symId, start, size, binaryId, binaryPathId, actualPathId, isKernel));
        }
    } else {
        if (isKernel) {
            const auto entry = m_unwind->findKallsymEntry(addressLocation.address);
            off = addressLocation.address - entry.address;
            symname = entry.symbol;
            if (!entry.module.isEmpty())
                binaryId = m_unwind->resolveString(entry.module);
        } else {
            symname = symbolFromPerfMap(addressLocation.address, &off);
        }

        if (off)
            functionLocation.address -= off;
        else
            functionLocation.address = elfStart;
        addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);
        if (!m_unwind->hasSymbol(addressLocation.parentLocationId)) {
            qint32 symId = m_unwind->resolveString(symname);
            m_unwind->resolveSymbol(addressLocation.parentLocationId,
                                    PerfUnwind::Symbol(symId, start, size, binaryId, binaryPathId, actualPathId, isKernel));
        }
    }
    // relAddr - relative address of the function start added with offset from the function start
    addressLocation.relAddr = relAddr;
    Q_ASSERT(addressLocation.parentLocationId != -1);
    Q_ASSERT(m_unwind->hasSymbol(addressLocation.parentLocationId));

    int locationId = m_unwind->resolveLocation(addressLocation);
    *isInterworking = (symname == "$a" || symname == "$t");
    addressCache->cache(elf, ip, {locationId, *isInterworking}, &m_invalidAddressCache);
    return locationId;
}

static bool operator<(const PerfSymbolTable::PerfMapSymbol &a,
                      const PerfSymbolTable::PerfMapSymbol &b)
{
    return a.start < b.start;
}

QByteArray PerfSymbolTable::symbolFromPerfMap(quint64 ip, GElf_Off *offset) const
{
    QVector<PerfMapSymbol>::ConstIterator sym
            = std::upper_bound(m_perfMap.begin(), m_perfMap.end(), PerfMapSymbol(ip));
    if (sym != m_perfMap.begin()) {
        --sym;
        if (sym->start <= ip && sym->start + sym->length > ip) {
            *offset = ip - sym->start;
            return sym->name;
        }
    }

    *offset = 0;
    return QByteArray();
}

void PerfSymbolTable::updatePerfMap()
{
    if (!m_hasPerfMap)
        return;

    if (!m_perfMapFile.isOpen())
        m_perfMapFile.open(QIODevice::ReadOnly);

    bool readLine = false;
    while (!m_perfMapFile.atEnd()) {
        QByteArrayList line = m_perfMapFile.readLine().split(' ');
        if (line.length() >= 3) {
            bool ok = false;
            quint64 start = line.takeFirst().toULongLong(&ok, 16);
            if (!ok)
                continue;
            quint64 length = line.takeFirst().toULongLong(&ok, 16);
            if (!ok)
                continue;
            QByteArray name = line.join(' ').trimmed();
            m_perfMap.append(PerfMapSymbol(start, length, name));
            readLine = true;
        }
    }

    if (readLine)
        std::sort(m_perfMap.begin(), m_perfMap.end());
}

bool PerfSymbolTable::containsAddress(quint64 address) const
{
    return m_elfs.isAddressInRange(address);
}

Dwfl *PerfSymbolTable::attachDwfl(const Dwfl_Thread_Callbacks *callbacks, PerfUnwind::UnwindInfo *unwindInfo)
{
    if (static_cast<pid_t>(m_pid) == dwfl_pid(m_dwfl))
        return m_dwfl; // Already attached, nothing to do

    // only attach state when we have the required information for stack unwinding
    // for normal symbol resolution and inline frame resolution this is not needed
    // most notably, this isn't needed for frame pointer callchains
    const auto sampleType = unwindInfo->sample->type();
    const auto hasSampleRegsUser = (sampleType & PerfEventAttributes::SAMPLE_REGS_USER);
    const auto hasSampleStackUser = (sampleType & PerfEventAttributes::SAMPLE_STACK_USER);
    if (!hasSampleRegsUser || !hasSampleStackUser)
        return nullptr;

    if (!dwfl_attach_state(m_dwfl, m_firstElf.elf(), m_pid, callbacks, unwindInfo)) {
        qWarning() << m_pid << "failed to attach state" << dwfl_errmsg(dwfl_errno());
        return nullptr;
    }

    return m_dwfl;
}

void PerfSymbolTable::clearCache()
{
    m_invalidAddressCache.clear();
    m_cuDieRanges.clear();
    m_perfMap.clear();
    if (m_perfMapFile.isOpen())
        m_perfMapFile.reset();

    // Throw out the dwfl state
    dwfl_report_begin(m_dwfl);
    const int reportEnd = dwfl_report_end(m_dwfl, NULL, NULL);
    Q_ASSERT(reportEnd == 0);

    m_cacheIsDirty = false;
}

PerfSymbolTable::ElfAndFile &PerfSymbolTable::ElfAndFile::operator=(
            PerfSymbolTable::ElfAndFile &&other)
{
    if (&other != this) {
        clear();
        m_elf = other.m_elf;
        m_file = other.m_file;
        m_fullPath = std::move(other.m_fullPath);
        other.m_elf = nullptr;
        other.m_file = -1;
    }
    return *this;
}

PerfSymbolTable::ElfAndFile::~ElfAndFile()
{
    clear();
}

void PerfSymbolTable::ElfAndFile::clear()
{
    if (m_elf)
        elf_end(m_elf);

    if (m_file != -1)
        eu_compat_close(m_file);
}

PerfSymbolTable::ElfAndFile::ElfAndFile(const QFileInfo &fullPath)
    : m_fullPath(fullPath)
{
    m_file = eu_compat_open(fullPath.absoluteFilePath().toLocal8Bit().constData(),
                            O_RDONLY | O_BINARY);
    if (m_file == -1)
        return;

    m_elf = elf_begin(m_file, ELF_C_READ, nullptr);
    if (m_elf && elf_kind(m_elf) == ELF_K_NONE) {
        elf_end(m_elf);
        m_elf = nullptr;
    }
}

PerfSymbolTable::ElfAndFile::ElfAndFile(PerfSymbolTable::ElfAndFile &&other)
    : m_elf(other.m_elf), m_file(other.m_file), m_fullPath(std::move(other.m_fullPath))
{
    other.m_elf = nullptr;
    other.m_file = -1;
}

void PerfSymbolTable::initAfterFork(const PerfSymbolTable* parent)
{
    m_elfs.copyDataFrom(&parent->m_elfs);
    m_firstElf = ElfAndFile(parent->m_firstElf.fullPath());
}
