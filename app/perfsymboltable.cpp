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
#include <QStack>

#include <tuple>
#include <cstring>

#include <dwarf.h>

PerfSymbolTable::PerfSymbolTable(qint32 pid, Dwfl_Callbacks *callbacks, PerfUnwind *parent) :
    m_perfMapFile(QDir::tempPath() + QDir::separator()
                  + QString::fromLatin1("perf-%1.map").arg(pid)),
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

    dwfl_report_begin(m_dwfl);

    // "DWFL can not be used until this function returns 0"
    const int reportEnd = dwfl_report_end(m_dwfl, NULL, NULL);
    Q_ASSERT(reportEnd == 0);
}

PerfSymbolTable::~PerfSymbolTable()
{
    dwfl_end(m_dwfl);
}

static pid_t nextThread(Dwfl *dwfl, void *arg, void **threadArg)
{
    /* Stop after first thread. */
    if (*threadArg != nullptr)
        return 0;

    *threadArg = arg;
    return dwfl_pid(dwfl);
}

static void *memcpyTarget(Dwarf_Word *result, int wordWidth)
{
    if (wordWidth == 4)
        return (uint32_t *)result;

    Q_ASSERT(wordWidth == 8);
    return result;
}

static void doMemcpy(Dwarf_Word *result, const void *src, int wordWidth)
{
    Q_ASSERT(wordWidth > 0);
    *result = 0; // initialize, as we might only overwrite half of it
    std::memcpy(memcpyTarget(result, wordWidth), src, static_cast<size_t>(wordWidth));
}

static quint64 registerAbi(const PerfRecordSample *sample)
{
    const quint64 abi = sample->registerAbi();
    Q_ASSERT(abi > 0); // ABI 0 means "no registers" - we shouldn't unwind in this case.
    return abi - 1;
}

static bool accessDsoMem(const PerfUnwind::UnwindInfo *ui, Dwarf_Addr addr,
                         Dwarf_Word *result, int wordWidth)
{
    Q_ASSERT(wordWidth > 0);
    // TODO: Take the pgoff into account? Or does elf_getdata do that already?
    auto mod = ui->unwind->symbolTable(ui->sample->pid())->module(addr);
    if (!mod)
        return false;

    Dwarf_Addr bias;
    Elf_Scn *section = dwfl_module_address_section(mod, &addr, &bias);

    if (section) {
        Elf_Data *data = elf_getdata(section, nullptr);
        if (data && data->d_buf && data->d_size > addr) {
            doMemcpy(result, static_cast<char *>(data->d_buf) + addr, wordWidth);
            return true;
        }
    }

    return false;
}

static bool memoryRead(Dwfl *, Dwarf_Addr addr, Dwarf_Word *result, void *arg)
{
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);
    const int wordWidth =
            PerfRegisterInfo::s_wordWidth[ui->unwind->architecture()][registerAbi(ui->sample)];

    /* Check overflow. */
    if (addr + sizeof(Dwarf_Word) < addr) {
        qDebug() << "Invalid memory read requested by dwfl" << Qt::hex << addr;
        ui->firstGuessedFrame = ui->frames.length();
        return false;
    }

    const QByteArray &stack = ui->sample->userStack();

    quint64 start = ui->sample->registerValue(
                PerfRegisterInfo::s_perfSp[ui->unwind->architecture()]);
    Q_ASSERT(stack.size() >= 0);
    quint64 end = start + static_cast<quint64>(stack.size());

    if (addr < start || addr + sizeof(Dwarf_Word) > end) {
        // not stack, try reading from ELF
        if (ui->unwind->ipIsInKernelSpace(addr)) {
            // DWARF unwinding is not done for the kernel
            qWarning() << "DWARF unwind tried to access kernel space" << Qt::hex << addr;
            return false;
        }
        if (!accessDsoMem(ui, addr, result, wordWidth)) {
            ui->firstGuessedFrame = ui->frames.length();
            const QHash<quint64, Dwarf_Word> &stackValues = ui->stackValues[ui->sample->pid()];
            auto it = stackValues.find(addr);
            if (it == stackValues.end()) {
                return false;
            } else {
                *result = *it;
            }
        }
    } else {
        doMemcpy(result, &(stack.data()[addr - start]), wordWidth);
        ui->stackValues[ui->sample->pid()][addr] = *result;
    }
    return true;
}

static bool setInitialRegisters(Dwfl_Thread *thread, void *arg)
{
    const PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);
    const quint64 abi = registerAbi(ui->sample);
    const uint architecture = ui->unwind->architecture();
    const int numRegs = PerfRegisterInfo::s_numRegisters[architecture][abi];
    Q_ASSERT(numRegs >= 0);
    QVarLengthArray<Dwarf_Word, 64> dwarfRegs(numRegs);
    for (int i = 0; i < numRegs; ++i) {
        dwarfRegs[i] = ui->sample->registerValue(
                    PerfRegisterInfo::s_perfToDwarf[architecture][abi][i]);
    }

    // Go one frame up to get the rest of the stack at interworking veneers.
    if (ui->isInterworking) {
        dwarfRegs[static_cast<int>(PerfRegisterInfo::s_dwarfIp[architecture][abi])] =
                dwarfRegs[static_cast<int>(PerfRegisterInfo::s_dwarfLr[architecture][abi])];
    }

    int dummyBegin = PerfRegisterInfo::s_dummyRegisters[architecture][0];
    int dummyNum = PerfRegisterInfo::s_dummyRegisters[architecture][1] - dummyBegin;

    if (dummyNum > 0) {
        QVarLengthArray<Dwarf_Word, 64> dummyRegs(dummyNum);
        std::memset(dummyRegs.data(), 0, static_cast<size_t>(dummyNum) * sizeof(Dwarf_Word));
        if (!dwfl_thread_state_registers(thread, dummyBegin, static_cast<uint>(dummyNum),
                                         dummyRegs.data()))
            return false;
    }

    return dwfl_thread_state_registers(thread, 0, static_cast<uint>(numRegs), dwarfRegs.data());
}

static const Dwfl_Thread_Callbacks threadCallbacks = {
    nextThread, nullptr, memoryRead, setInitialRegisters, nullptr, nullptr
};

static bool findInExtraPath(QFileInfo &path, const QString &fileName)
{
    path.setFile(path.absoluteFilePath() + QDir::separator() + fileName);
    if (path.isFile())
        return true;

    QDir absDir = path.absoluteDir();
    foreach (const QString &entry, absDir.entryList(QStringList(),
                                                    QDir::Dirs | QDir::NoDotAndDotDot)) {
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

QFileInfo PerfSymbolTable::findFile(const char *path, const QString &fileName,
                                    const QByteArray &buildId) const
{
    QFileInfo fullPath;
    // first try to find the debug information via build id, if available
    if (!buildId.isEmpty()) {
        const QString buildIdPath = QString::fromUtf8(path) + QDir::separator()
                + QString::fromUtf8(buildId.toHex());
        foreach (const QString &extraPath, splitPath(m_unwind->debugPath())) {
            fullPath.setFile(extraPath);
            if (findBuildIdPath(fullPath, buildIdPath))
                return fullPath;
        }
    }

    if (!m_unwind->appPath().isEmpty()) {
        // try to find the file in the app path
        fullPath.setFile(m_unwind->appPath());
        if (findInExtraPath(fullPath, fileName))
            return fullPath;
    }

    // try to find the file in the extra libs path
    foreach (const QString &extraPath, splitPath(m_unwind->extraLibsPath())) {
        fullPath.setFile(extraPath);
        if (findInExtraPath(fullPath, fileName))
            return fullPath;
    }

    // last fall-back, try the system root
    fullPath.setFile(m_unwind->systemRoot() + QString::fromUtf8(path));
    return fullPath;
}

void PerfSymbolTable::registerElf(const PerfRecordMmap &mmap, const QByteArray &buildId)
{
    QString filePath(mmap.filename());
    // special regions, such as [heap], [vdso], [stack], [kernel.kallsyms]_text ... as well as //anon
    const bool isSpecialRegion = (filePath.startsWith('[') && filePath.contains(']'))
                              || filePath.startsWith(QLatin1String("/dev/"))
                              || filePath.startsWith(QLatin1String("/memfd:"))
                              || filePath.startsWith(QLatin1String("/SYSV"))
                              || filePath == QLatin1String("//anon");
    const auto fileName = isSpecialRegion ? QString() : QFileInfo(filePath).fileName();
    QFileInfo fullPath;
    if (isSpecialRegion) {
        // don not set fullPath, these regions don't represent a real file
    } else if (mmap.pid() != PerfUnwind::s_kernelPid) {
        fullPath = findFile(mmap.filename(), fileName, buildId);

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
    const QByteArray file = dwarf_decl_file(top);

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
        const QByteArray file
                = (dwarf_formudata(dwarf_attr(top, DW_AT_call_file, &attr), &val) == 0)
                ? dwarf_filesrc (files, val, nullptr, nullptr) : "";
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

static QFileInfo findDebugInfoFile(const QString &root, const QString &file,
                                   const QString &debugLinkString)
{
    auto dir = QFileInfo(root).dir();
    const auto folder = QFileInfo(file).path();

    QFileInfo debugLinkFile;

    if (!folder.isEmpty()) {
        debugLinkFile.setFile(dir, folder + QDir::separator() + debugLinkString);
        if (debugLinkFile.isFile())
            return debugLinkFile;
    }

    debugLinkFile.setFile(dir, file + QDir::separator() + debugLinkString);
    if (debugLinkFile.isFile())
        return debugLinkFile;

    // try again in .debug folder
    if (!folder.isEmpty()) {
        debugLinkFile.setFile(dir, folder + QDir::separator() + QLatin1String(".debug")
                                    + QDir::separator() + debugLinkString);
        if (debugLinkFile.isFile())
            return debugLinkFile;
    }

    debugLinkFile.setFile(dir, file + QDir::separator() + QLatin1String(".debug")
                                + QDir::separator() + debugLinkString);
    if (debugLinkFile.isFile())
        return debugLinkFile;

    // try again in /usr/lib/debug folder
    debugLinkFile.setFile(dir, QLatin1String("usr") + QDir::separator() + QLatin1String("lib")
                                + QDir::separator() + QLatin1String("debug") + QDir::separator() + folder
                                + QDir::separator() + debugLinkString);

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
    const auto debugLinkString = QFile(debugLink).fileName();
    auto debugLinkFile = findFile(debugLink, debugLinkString);
    if (!debugLinkFile.isFile()) {
        // fall-back to original file path with debug link file name
        const auto &elf = m_elfs.findElf(base);
        const auto &path = QString::fromUtf8(elf.originalPath);
        debugLinkFile = findDebugInfoFile(m_unwind->systemRoot(), path, debugLinkString);
    }

    /// FIXME: find a proper solution to this
    if (!debugLinkFile.isFile() && QByteArray(file).endsWith("/elf")) {
        // fall-back to original file if it's in a build-id path
        debugLinkFile.setFile(file);
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

int symbolIndex(const Elf64_Rel &rel)
{
    return ELF64_R_SYM(rel.r_info);
}

int symbolIndex(const Elf64_Rela &rel)
{
    return ELF64_R_SYM(rel.r_info);
}

int symbolIndex(const Elf32_Rel &rel)
{
    return ELF32_R_SYM(rel.r_info);
}

int symbolIndex(const Elf32_Rela &rel)
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
    if (!str || str == QLatin1String(".text"))
        return {};

    if (str == QLatin1String(".plt") && entsize > 0) {
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

static quint64 symbolAddress(quint64 addr, bool isArmArch)
{
    // For dwfl API call we need the raw pointer into symtab, so we need to adjust ip.
    return (!isArmArch || (addr & 1)) ? addr : addr + 1;
}

static quint64 alignedAddress(quint64 addr, bool isArmArch)
{
    // Adjust addr back. The symtab entries are 1 off for all practical purposes.
    return (isArmArch && (addr & 1)) ? addr - 1 : addr;
}

static PerfAddressCache::SymbolCache cacheSymbols(Dwfl_Module *module, quint64 elfStart, bool isArmArch)
{
    PerfAddressCache::SymbolCache cache;

    const auto numSymbols = dwfl_module_getsymtab(module);
    for (int i = 0; i < numSymbols; ++i) {
        GElf_Sym sym;
        GElf_Addr symAddr;
        const auto symbol = dwfl_module_getsym_info(module, i, &sym, &symAddr, nullptr, nullptr, nullptr);
        if (symbol) {
            const quint64 start = alignedAddress(sym.st_value, isArmArch);
            cache.append({symAddr - elfStart, start, sym.st_size, symbol});
        }
    }
    return cache;
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
    PerfUnwind::Location addressLocation(symbolAddress(ip, isArmArch), 0, -1, m_pid);
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
            addressCache->setSymbolCache(elf.originalPath, cacheSymbols(mod, elfStart, isArmArch));
        }

        auto cachedAddrInfo = addressCache->findSymbol(elf.originalPath, addressLocation.address - elfStart);
        if (cachedAddrInfo.isValid()) {
            off = addressLocation.address - elfStart - cachedAddrInfo.offset;
            symname = cachedAddrInfo.symname;
            start = cachedAddrInfo.value;
            size = cachedAddrInfo.size;
            relAddr = alignedAddress(start + off, isArmArch);

            Dwarf_Addr bias = 0;
            functionLocation.address -= off; // in case we don't find anything better

            if (!m_cuDieRanges.contains(mod))
                m_cuDieRanges[mod] = PerfDwarfDieCache(mod);

            auto *cudie = m_cuDieRanges[mod].findCuDie(addressLocation.address);
            if (cudie) {
                bias = cudie->bias();
                const auto offset = addressLocation.address - bias;
                auto srcloc = dwarf_getsrc_die(cudie->cudie(), offset);
                if (srcloc) {
                    const char* srcfile = dwarf_linesrc(srcloc, nullptr, nullptr);
                    if (srcfile) {
                        const QByteArray file = srcfile;
                        addressLocation.file = m_unwind->resolveString(file);
                        dwarf_lineno(srcloc, &addressLocation.line);
                        dwarf_linecol(srcloc, &addressLocation.column);
                    }
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
                        functionLocation.file = m_unwind->resolveString(dwarf_decl_file(&die));
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
    if (!m_perfMapFile.exists())
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

Dwfl *PerfSymbolTable::attachDwfl(void *arg)
{
    if (static_cast<pid_t>(m_pid) == dwfl_pid(m_dwfl))
        return m_dwfl; // Already attached, nothing to do

    // only attach state when we have the required information for stack unwinding
    // for normal symbol resolution and inline frame resolution this is not needed
    // most notably, this isn't needed for frame pointer callchains
    PerfUnwind::UnwindInfo *unwindInfo = static_cast<PerfUnwind::UnwindInfo *>(arg);
    const auto sampleType = unwindInfo->sample->type();
    const auto hasSampleRegsUser = (sampleType & PerfEventAttributes::SAMPLE_REGS_USER);
    const auto hasSampleStackUser = (sampleType & PerfEventAttributes::SAMPLE_STACK_USER);
    if (!hasSampleRegsUser || !hasSampleStackUser)
        return nullptr;

    if (!dwfl_attach_state(m_dwfl, m_firstElf.elf(), m_pid, &threadCallbacks, arg)) {
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
