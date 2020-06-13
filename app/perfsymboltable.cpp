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

#include <dwarf.h>

#include <QDebug>
#include <QDir>
#include <QStack>

#include <tuple>
#include <cstring>
#include <fcntl.h>

#ifdef Q_OS_WIN
#include <libeu_compat.h>
#else
#include <cxxabi.h>
#include <unistd.h>
#define eu_compat_open open
#define eu_compat_close close
#define eu_compat_malloc malloc
#define eu_compat_free free
#define eu_compat_demangle abi::__cxa_demangle
#define eu_compat_strdup strdup
#define O_BINARY 0
#endif

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
        qDebug() << "Invalid memory read requested by dwfl" << hex << addr;
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
            qWarning() << "DWARF unwind tried to access kernel space" << hex << addr;
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
    return path.split(QDir::listSeparator(), QString::SkipEmptyParts);
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

static QByteArray demangle(const QByteArray &mangledName)
{
    if (mangledName.length() < 3) {
        return mangledName;
    } else {
        static size_t demangleBufferLength = 0;
        static char *demangleBuffer = nullptr;

        // Require GNU v3 ABI by the "_Z" prefix.
        if (mangledName[0] == '_' && mangledName[1] == 'Z') {
            int status = -1;
            char *dsymname = eu_compat_demangle(mangledName, demangleBuffer, &demangleBufferLength,
                                            &status);
            if (status == 0)
                return demangleBuffer = dsymname;
        }
    }
    return mangledName;
}

/// @return the fully qualified linkage name
static const char *linkageName(Dwarf_Die *die)
{
    Dwarf_Attribute attr;
    Dwarf_Attribute *result = dwarf_attr_integrate(die, DW_AT_MIPS_linkage_name, &attr);
    if (!result)
        result = dwarf_attr_integrate(die, DW_AT_linkage_name, &attr);

    return result ? dwarf_formstring(result) : nullptr;
}

/// @return the referenced DW_AT_specification DIE
/// inlined subroutines of e.g. std:: algorithms aren't namespaced, but their DW_AT_specification DIE is
static Dwarf_Die *specificationDie(Dwarf_Die *die, Dwarf_Die *dieMem)
{
    Dwarf_Attribute attr;
    if (dwarf_attr_integrate(die, DW_AT_specification, &attr))
        return dwarf_formref_die(&attr, dieMem);
    return nullptr;
}

/// prepend the names of all scopes that reference the @p die to @p name
static void prependScopeNames(QByteArray &name, Dwarf_Die *die)
{
    Dwarf_Die dieMem;
    Dwarf_Die *scopes = nullptr;
    auto nscopes = dwarf_getscopes_die(die, &scopes);

    // skip scope for the die itself at the start and the compile unit DIE at end
    for (int i = 1; i < nscopes - 1; ++i) {
        auto scope = scopes + i;

        if (auto scopeLinkageName = linkageName(scope)) {
            // prepend the fully qualified linkage name
            name.prepend("::");
            // we have to demangle the scope linkage name, otherwise we get a
            // mish-mash of mangled and non-mangled names
            name.prepend(demangle(scopeLinkageName));
            // we can stop now, the scope is fully qualified
            break;
        }

        if (auto scopeName = dwarf_diename(scope)) {
            // prepend this scope's name, e.g. the class or namespace name
            name.prepend("::");
            name.prepend(scopeName);
        }

        if (auto specification = specificationDie(scope, &dieMem)) {
            eu_compat_free(scopes);
            scopes = nullptr;
            // follow the scope's specification DIE instead
            prependScopeNames(name, specification);
            break;
        }
    }

    eu_compat_free(scopes);
}

static QByteArray dieName(Dwarf_Die *die)
{
    // linkage names are fully qualified, meaning we can stop early then
    if (auto name = linkageName(die))
        return name;

    // otherwise do a more complex lookup that includes namespaces and other context information
    // this is important for inlined subroutines such as lambdas or std:: algorithms
    QByteArray name = dwarf_diename(die);

    // use the specification DIE which is within the DW_TAG_namespace
    Dwarf_Die dieMem;
    if (auto specification = specificationDie(die, &dieMem))
        die = specification;

    prependScopeNames(name, die);
    return name;
}

int PerfSymbolTable::insertSubprogram(Dwarf_Die *top, Dwarf_Addr entry, qint32 binaryId, qint32 binaryPathId,
                                      qint32 inlineCallLocationId, bool isKernel)
{
    int line = 0;
    dwarf_decl_line(top, &line);
    int column = 0;
    dwarf_decl_column(top, &column);
    const QByteArray file = dwarf_decl_file(top);

    qint32 fileId = m_unwind->resolveString(file);
    int locationId = m_unwind->resolveLocation(PerfUnwind::Location(entry, fileId, m_pid, line,
                                                                    column, inlineCallLocationId));
    qint32 symId = m_unwind->resolveString(demangle(dieName(top)));
    m_unwind->resolveSymbol(locationId, PerfUnwind::Symbol(symId, binaryId, binaryPathId, isKernel));

    return locationId;
}

int PerfSymbolTable::parseDie(Dwarf_Die *top, qint32 binaryId, qint32 binaryPathId, bool isKernel,
                              Dwarf_Files *files, Dwarf_Addr entry, qint32 parentLocationId)
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
        return insertSubprogram(top, entry, binaryId, binaryPathId, callLocationId, isKernel);
    }
    case DW_TAG_subprogram:
        return insertSubprogram(top, entry, binaryId, binaryPathId, -1, isKernel);
    default:
        return -1;
    }
}

qint32 PerfSymbolTable::parseDwarf(Dwarf_Die *cudie, Dwarf_Die *subroutine, Dwarf_Addr bias, qint32 binaryId,
                                   qint32 binaryPathId, bool isKernel)
{
    Dwarf_Die *scopes = nullptr;
    const auto nscopes = dwarf_getscopes_die(subroutine, &scopes);

    Dwarf_Files *files = nullptr;
    dwarf_getsrcfiles(cudie, &files, nullptr);

    qint32 parentLocationId = -1;
    for (int i = nscopes - 1; i >= 0; --i) {
        const auto scope = &scopes[i];
        Dwarf_Addr scopeAddr = bias;
        Dwarf_Addr entry = 0;
        if (dwarf_entrypc(scope, &entry) == 0 && entry != 0)
            scopeAddr += entry;

        auto locationId = parseDie(scope, binaryId, binaryPathId, isKernel, files, scopeAddr, parentLocationId);
        if (locationId != -1)
            parentLocationId = locationId;
    }

    eu_compat_free(scopes);
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
        qWarning() << "stale base mapping referenced:" << elf << base << dec << m_pid << hex << addr;
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

class CuDieRanges
{
public:
    struct CuDieRange
    {
        Dwarf_Die *cuDie;
        Dwarf_Addr bias;
        Dwarf_Addr low;
        Dwarf_Addr high;

        bool contains(Dwarf_Addr addr) const
        {
            return low <= addr && addr < high;
        }
    };

    CuDieRanges(Dwfl_Module *mod = nullptr)
    {
        if (!mod)
            return;

        Dwarf_Die *die = nullptr;
        Dwarf_Addr bias = 0;
        while ((die = dwfl_module_nextcu(mod, die, &bias))) {
            Dwarf_Addr low = 0;
            Dwarf_Addr high = 0;
            Dwarf_Addr base = 0;
            ptrdiff_t offset = 0;
            while ((offset = dwarf_ranges(die, offset, &base, &low, &high)) > 0) {
                ranges.push_back(CuDieRange{die, bias, low + bias, high + bias});
            }
        }
    }

    Dwarf_Die *findDie(Dwarf_Addr addr, Dwarf_Addr *bias) const
    {
        auto it = std::find_if(ranges.begin(), ranges.end(),
                               [addr](const CuDieRange &range) {
                                    return range.contains(addr);
                               });
        if (it == ranges.end())
            return nullptr;

        *bias = it->bias;
        return it->cuDie;
    }
public:
    QVector<CuDieRange> ranges;
};
QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(CuDieRanges, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(CuDieRanges::CuDieRange, Q_MOVABLE_TYPE);
QT_END_NAMESPACE

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
    quint64 elfStart = 0;
    if (elf.isValid()) {
        binaryId = m_unwind->resolveString(elf.originalFileName);
        binaryPathId = m_unwind->resolveString(elf.originalPath);
        elfStart = elf.hasBaseAddr() ? elf.baseAddr : elf.addr;
    }

    Dwfl_Module *mod = module(ip, elf);

    PerfUnwind::Location addressLocation(
                (m_unwind->architecture() != PerfRegisterInfo::ARCH_ARM || (ip & 1))
                ? ip : ip + 1, -1, m_pid);
    PerfUnwind::Location functionLocation(addressLocation);

    QByteArray symname;
    GElf_Off off = 0;

    if (mod) {
        auto cachedAddrInfo = addressCache->findSymbol(elf, addressLocation.address);
        if (cachedAddrInfo.isValid()) {
            off = addressLocation.address - elf.addr - cachedAddrInfo.offset;
            symname = cachedAddrInfo.symname;
        } else {
            GElf_Sym sym;
            // For addrinfo we need the raw pointer into symtab, so we need to adjust ourselves.
            symname = dwfl_module_addrinfo(mod, addressLocation.address, &off, &sym, nullptr, nullptr,
                                           nullptr);
            if (off != addressLocation.address)
                addressCache->cacheSymbol(elf, addressLocation.address - off, sym.st_size, symname);
        }

        if (off == addressLocation.address) {// no symbol found
            symname = fakeSymbolFromSection(mod, addressLocation.address);
            addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);
        } else {
            Dwarf_Addr bias = 0;
            functionLocation.address -= off; // in case we don't find anything better

            auto die = dwfl_module_addrdie(mod, addressLocation.address, &bias);
            if (!die) {
                // broken DWARF emitter by clang, e.g. no aranges
                // cf.: https://sourceware.org/ml/elfutils-devel/2017-q2/msg00180.html
                // build a custom lookup table and query that one
                if (!m_cuDieRanges.contains(mod)) {
                    m_cuDieRanges[mod] = CuDieRanges(mod);
                }
                const auto& maps = m_cuDieRanges[mod];
                die = maps.findDie(addressLocation.address, &bias);
            }

            if (die) {
                auto srcloc = dwarf_getsrc_die(die, addressLocation.address - bias);
                if (srcloc) {
                    const char* srcfile = dwarf_linesrc(srcloc, nullptr, nullptr);
                    if (srcfile) {
                        const QByteArray file = srcfile;
                        addressLocation.file = m_unwind->resolveString(file);
                        dwarf_lineno(srcloc, &addressLocation.line);
                        dwarf_linecol(srcloc, &addressLocation.column);
                    }
                }
            }

            Dwarf_Die *subroutine = nullptr;
            Dwarf_Die *scopes = nullptr;
            int nscopes = dwarf_getscopes(die, addressLocation.address - bias, &scopes);
            for (int i = 0; i < nscopes; ++i) {
                Dwarf_Die *scope = &scopes[i];
                const int tag = dwarf_tag(scope);
                if (tag == DW_TAG_subprogram || tag == DW_TAG_inlined_subroutine) {
                    Dwarf_Addr entry = 0;
                    dwarf_entrypc(scope, &entry);
                    symname = dieName(scope); // use name of inlined function as symbol
                    functionLocation.address = entry + bias;
                    functionLocation.file = m_unwind->resolveString(dwarf_decl_file(scope));
                    dwarf_decl_line(scope, &functionLocation.line);
                    dwarf_decl_column(scope, &functionLocation.column);

                    subroutine = scope;
                    break;
                }
            }

            // check if the inline chain was cached already
            addressLocation.parentLocationId = m_unwind->lookupLocation(functionLocation);
            // otherwise resolve the inline chain if possible
            if (subroutine && !m_unwind->hasSymbol(addressLocation.parentLocationId))
                functionLocation.parentLocationId = parseDwarf(die, subroutine, bias, binaryId, binaryPathId, isKernel);
            // then resolve and cache the inline chain
            if (addressLocation.parentLocationId == -1)
                addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);

            eu_compat_free(scopes);
        }
        if (!m_unwind->hasSymbol(addressLocation.parentLocationId)) {
            // no sufficient debug information. Use what we already know
            qint32 symId = m_unwind->resolveString(demangle(symname));
            m_unwind->resolveSymbol(addressLocation.parentLocationId,
                                    PerfUnwind::Symbol(symId, binaryId, binaryPathId, isKernel));
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
                                    PerfUnwind::Symbol(symId, binaryId, binaryPathId, isKernel));
        }
    }

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
    : m_elf(other.m_elf), m_file(other.m_file)
{
    other.m_elf = nullptr;
    other.m_file = -1;
}
