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

#include <QDir>
#include <QDebug>
#include <QStack>

#include <cstring>
#include <fcntl.h>
#ifdef Q_OS_WIN
#include <io.h>
extern "C" {
    extern char *eu_compat_demangle(const char *mangled_name, char *output_buffer,
                                    size_t *length, int *status);
    extern int eu_compat_open(const char *, int);
    extern int eu_compat_close(int);
    extern void *eu_compat_malloc(size_t);
    extern void eu_compat_free(void *);
    static char* eu_compat_strdup(const char* string)
    {
        const size_t length = strlen(string) + 1; // include null char
        char* ret = reinterpret_cast<char*>(eu_compat_malloc(length));
        std::memcpy(ret, string, length);
        return ret;
    }
}
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

PerfSymbolTable::PerfSymbolTable(quint32 pid, Dwfl_Callbacks *callbacks, PerfUnwind *parent) :
    m_perfMapFile(QDir::tempPath() + QDir::separator()
                  + QString::fromLatin1("perf-%1.map").arg(pid)),
    m_cacheIsDirty(false),
    m_unwind(parent),
    m_firstElfFile(-1),
    m_firstElf(nullptr),
    m_callbacks(callbacks),
    m_pid(pid)
{
    m_dwfl = dwfl_begin(m_callbacks);
}

PerfSymbolTable::~PerfSymbolTable()
{
    dwfl_end(m_dwfl);
    elf_end(m_firstElf);
    eu_compat_close(m_firstElfFile);
}

static pid_t nextThread(Dwfl *dwfl, void *arg, void **threadArg)
{
    /* Stop after first thread. */
    if (*threadArg != 0)
        return 0;

    *threadArg = arg;
    return dwfl_pid(dwfl);
}

static void *memcpyTarget(Dwarf_Word *result, uint wordWidth)
{
    if (wordWidth == 4)
        return (uint32_t *)result;

    Q_ASSERT(wordWidth == 8);
    return result;
}

static void doMemcpy(Dwarf_Word *result, const void *src, uint wordWidth)
{
    *result = 0; // initialize, as we might only overwrite half of it
    std::memcpy(memcpyTarget(result, wordWidth), src, wordWidth);
}

static uint registerAbi(const PerfRecordSample *sample)
{
    const uint abi = sample->registerAbi();
    Q_ASSERT(abi > 0); // ABI 0 means "no registers" - we shouldn't unwind in this case.
    return abi - 1;
}

static bool accessDsoMem(Dwfl *dwfl, const PerfUnwind::UnwindInfo *ui, Dwarf_Addr addr,
                         Dwarf_Word *result, uint wordWidth)
{
    // TODO: Take the pgoff into account? Or does elf_getdata do that already?
    auto mod = ui->unwind->symbolTable(ui->sample->pid())->module(addr);

    Dwarf_Addr bias;
    Elf_Scn *section = dwfl_module_address_section(mod, &addr, &bias);

    if (section) {
        Elf_Data *data = elf_getdata(section, NULL);
        if (data && data->d_buf && data->d_size > addr) {
            doMemcpy(result, static_cast<char *>(data->d_buf) + addr, wordWidth);
            return true;
        }
    }

    return false;
}

static bool memoryRead(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg)
{
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);
    const uint wordWidth =
            PerfRegisterInfo::s_wordWidth[ui->unwind->architecture()][registerAbi(ui->sample)];

    /* Check overflow. */
    if (addr > std::numeric_limits<Dwarf_Addr>::max() - sizeof(Dwarf_Word)) {
        qDebug() << "Invalid memory read requested by dwfl" << addr;
        ui->firstGuessedFrame = ui->frames.length();
        return false;
    }

    const QByteArray &stack = ui->sample->userStack();

    quint64 start = ui->sample->registerValue(
                PerfRegisterInfo::s_perfSp[ui->unwind->architecture()]);
    quint64 end = start + stack.size();

    if (addr < start || addr + sizeof(Dwarf_Word) > end) {
        // not stack, try reading from ELF
        if (ui->unwind->ipIsInKernelSpace(addr))
            dwfl = ui->unwind->dwfl(PerfUnwind::s_kernelPid);
        if (!accessDsoMem(dwfl, ui, addr, result, wordWidth)) {
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
    const uint abi = registerAbi(ui->sample);
    const uint architecture = ui->unwind->architecture();
    const uint numRegs = PerfRegisterInfo::s_numRegisters[architecture][abi];
    QVarLengthArray<Dwarf_Word, 64> dwarfRegs(numRegs);
    for (uint i = 0; i < numRegs; ++i) {
        dwarfRegs[i] = ui->sample->registerValue(
                    PerfRegisterInfo::s_perfToDwarf[architecture][abi][i]);
    }

    // Go one frame up to get the rest of the stack at interworking veneers.
    if (ui->isInterworking) {
        dwarfRegs[PerfRegisterInfo::s_dwarfIp[architecture][abi]] =
                dwarfRegs[PerfRegisterInfo::s_dwarfLr[architecture][abi]];
    }

    uint dummyBegin = PerfRegisterInfo::s_dummyRegisters[architecture][0];
    uint dummyNum = PerfRegisterInfo::s_dummyRegisters[architecture][1] - dummyBegin;

    if (dummyNum > 0) {
        QVarLengthArray<Dwarf_Word, 64> dummyRegs(dummyNum);
        std::memset(dummyRegs.data(), 0, dummyNum * sizeof(Dwarf_Word));
        if (!dwfl_thread_state_registers(thread, dummyBegin, dummyNum, dummyRegs.data()))
            return false;
    }

    return dwfl_thread_state_registers(thread, 0, numRegs, dwarfRegs.data());
}

static const Dwfl_Thread_Callbacks threadCallbacks = {
    nextThread, NULL, memoryRead, setInitialRegisters, NULL, NULL
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
    QLatin1String filePath(mmap.filename());
    // special regions, such as [heap], [vdso], [stack], ... as well as //anon
    const bool isSpecialRegion = (mmap.filename().startsWith('[') && mmap.filename().endsWith(']'))
                              || filePath == QLatin1String("//anon")
                              || filePath == QLatin1String("/SYSV00000000 (deleted)");
    const auto fileName = QFileInfo(filePath).fileName();
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
        } else if (!m_firstElf) {
            m_firstElfFile = eu_compat_open(fullPath.absoluteFilePath().toLocal8Bit().constData(),
                                            O_RDONLY | O_BINARY);
            if (m_firstElfFile == -1) {
                qWarning() << "Failed to open file:" << fullPath.absoluteFilePath();
            } else {
                m_firstElf = elf_begin(m_firstElfFile, ELF_C_READ, nullptr);
                if (!m_firstElf) {
                    qWarning() << "Failed to begin elf:" << elf_errmsg(elf_errno());
                    eu_compat_close(m_firstElfFile);
                    m_firstElfFile = -1;
                } else if (elf_kind(m_firstElf) == ELF_K_NONE) {
                    // not actually an elf object
                    elf_end(m_firstElf);
                    m_firstElf = nullptr;
                    eu_compat_close(m_firstElfFile);
                    m_firstElfFile = -1;
                }
            }
        }
    } else { // kernel
        fullPath.setFile(m_unwind->systemRoot() + filePath);
    }

    bool cacheInvalid = m_elfs.registerElf(mmap.addr(), mmap.len(), mmap.pgoff(), fullPath,
                                           fileName.toUtf8(), mmap.filename());

    // There is no need to clear the symbol or location caches in PerfUnwind. Some locations become
    // stale this way, but we still need to keep their IDs, as the receiver might still use them for
    // past frames.
    if (cacheInvalid)
        clearCache();
}

static QByteArray dieName(Dwarf_Die *die)
{
    Dwarf_Attribute attr;
    Dwarf_Attribute *result = dwarf_attr_integrate(die, DW_AT_MIPS_linkage_name, &attr);
    if (!result)
        result = dwarf_attr_integrate(die, DW_AT_linkage_name, &attr);

    const char *name = dwarf_formstring(result);
    if (!name)
        name = dwarf_diename(die);

    return name ? name : "";
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

int PerfSymbolTable::insertSubprogram(Dwarf_Die *top, Dwarf_Addr entry, qint32 binaryId,
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
    m_unwind->resolveSymbol(locationId, PerfUnwind::Symbol(symId, binaryId, isKernel));

    return locationId;
}

int PerfSymbolTable::parseDie(Dwarf_Die *top, qint32 binaryId, Dwarf_Files *files,
                              Dwarf_Addr entry, bool isKernel, const QStack<DieAndLocation> &stack)
{
    int tag = dwarf_tag(top);
    switch (tag) {
    case DW_TAG_inlined_subroutine: {
        PerfUnwind::Location location(entry);
        Dwarf_Attribute attr;
        Dwarf_Word val = 0;
        const QByteArray file
                = (dwarf_formudata(dwarf_attr(top, DW_AT_call_file, &attr), &val) == 0)
                ? dwarf_filesrc (files, val, NULL, NULL) : "";
        location.file = m_unwind->resolveString(file);
        location.line
                = (dwarf_formudata(dwarf_attr(top, DW_AT_call_line, &attr), &val) == 0)
                ? val : -1;
        location.column
                = (dwarf_formudata(dwarf_attr(top, DW_AT_call_column, &attr), &val) == 0)
                ? val : -1;

        auto it = stack.end();
        --it;
        while (it != stack.begin()) {
            location.parentLocationId = (--it)->locationId;
            if (location.parentLocationId != -1)
                break;
        }

        int callLocationId = m_unwind->resolveLocation(location);
        return insertSubprogram(top, entry, binaryId, callLocationId, isKernel);
    }
    case DW_TAG_subprogram:
        return insertSubprogram(top, entry, binaryId, -1, isKernel);
    default:
        return -1;
    }
}

void PerfSymbolTable::parseDwarf(Dwarf_Die *cudie, Dwarf_Addr bias, qint32 binaryId,
                                 bool isKernel)
{
    // Iterate through all dwarf sections and establish parent/child relations for inline
    // subroutines. Add all symbols to m_symbols and special frames for start points of inline
    // instances to m_addresses.

    QStack<DieAndLocation> stack;
    stack.push_back({*cudie, -1});

    Dwarf_Files *files = 0;
    dwarf_getsrcfiles(cudie, &files, NULL);

    while (!stack.isEmpty()) {
        Dwarf_Die *top = &(stack.last().die);
        Dwarf_Addr entry = 0;
        if (dwarf_entrypc(top, &entry) == 0 && entry != 0)
            stack.last().locationId = parseDie(top, binaryId, files, entry + bias, isKernel, stack);

        Dwarf_Die child;
        if (dwarf_child(top, &child) == 0) {
            stack.push_back({child, -1});
        } else {
            do {
                Dwarf_Die sibling;
                // Mind that stack.last() can change during this loop. So don't use "top" below.
                const bool hasSibling = (dwarf_siblingof(&(stack.last().die), &sibling) == 0);
                stack.pop_back();
                if (hasSibling) {
                    stack.push_back({sibling, -1});
                    break;
                }
            } while (!stack.isEmpty());
        }
    }
}

static void reportError(const PerfElfMap::ElfInfo& info, const char *message)
{
    qWarning() << "failed to report" << info << "for"
               << hex << info.addr << dec << ":" << message;
}

Dwfl_Module *PerfSymbolTable::reportElf(const PerfElfMap::ElfInfo& info)
{
    if (!info.isValid() || !info.isFile())
        return nullptr;

    if (info.pgoff > 0) {
        reportError(info, "Cannot report file fragments");
        return nullptr;
    }

    Dwfl_Module *ret = dwfl_report_elf(
                m_dwfl, info.originalFileName.constData(),
                info.localFile.absoluteFilePath().toLocal8Bit().constData(), -1, info.addr,
                false);
    if (!ret) {
        reportError(info, dwfl_errmsg(dwfl_errno()));
        m_cacheIsDirty = true;
    } else {
        // set symbol table as user data, cf. find_debuginfo callback in perfunwind.cpp
        void** userData;
        dwfl_module_info(ret, &userData, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr);
        *userData = this;
    }

    return ret;
}

Dwfl_Module *PerfSymbolTable::module(quint64 addr)
{
    if (!m_dwfl)
        return nullptr;

    Dwfl_Module *mod = dwfl_addrmodule(m_dwfl, addr);
    const auto &elf = findElf(addr);

    if (mod) {
        // If dwfl has a module and it's not the same as what we want, report the module
        // we want. Many modules overlap ld.so, so if we've reported even one sample from
        // ld.so we would otherwise be blocked from reporting anything that overlaps it.
        Dwarf_Addr mod_start = 0;
        dwfl_module_info(mod, nullptr, &mod_start, nullptr, nullptr, nullptr, nullptr,
                         nullptr);
        if (elf.addr == mod_start)
            return mod;
    }
    return reportElf(elf);
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
        const auto &dir = QFileInfo(m_unwind->systemRoot() + QString::fromUtf8(elf.originalPath)).absoluteDir();
        debugLinkFile.setFile(dir, debugLinkString);
        if (!debugLinkFile.isFile()) // try again in .debug folder
            debugLinkFile.setFile(dir, QLatin1String(".debug") + QDir::separator() + debugLinkString);
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

// based on MIT licensed https://github.com/bombela/backward-cpp
static bool die_has_pc(Dwarf_Die* die, Dwarf_Addr pc)
{
    Dwarf_Addr low, high;

    // continuous range
    if (dwarf_hasattr(die, DW_AT_low_pc) && dwarf_hasattr(die, DW_AT_high_pc)) {
        if (dwarf_lowpc(die, &low) != 0)
            return false;
        if (dwarf_highpc(die, &high) != 0) {
            Dwarf_Attribute attr_mem;
            Dwarf_Attribute* attr = dwarf_attr(die, DW_AT_high_pc, &attr_mem);
            Dwarf_Word value;
            if (dwarf_formudata(attr, &value) != 0)
                return false;
            high = low + value;
        }
        return pc >= low && pc < high;
    }

    // non-continuous range.
    Dwarf_Addr base;
    ptrdiff_t offset = 0;
    while ((offset = dwarf_ranges(die, offset, &base, &low, &high)) > 0) {
        if (pc >= low && pc < high)
            return true;
    }
    return false;
}

static bool find_fundie_by_pc(Dwarf_Die* parent_die, Dwarf_Addr pc)
{
    Dwarf_Die die;
    if (dwarf_child(parent_die, &die) != 0)
        return false;

    do {
        switch (dwarf_tag(&die)) {
        case DW_TAG_subprogram:
        case DW_TAG_inlined_subroutine:
            if (die_has_pc(&die, pc))
                return true;
        };
        bool declaration = false;
        Dwarf_Attribute attr_mem;
        dwarf_formflag(dwarf_attr(&die, DW_AT_declaration, &attr_mem), &declaration);
        if (!declaration) {
            // let's be curious and look deeper in the tree,
            // function are not necessarily at the first level, but
            // might be nested inside a namespace, structure etc.
            if (find_fundie_by_pc(&die, pc))
                return true;
        }
    } while (dwarf_siblingof(&die, &die) == 0);
    return false;
}

Dwarf_Die *find_die(Dwfl_Module *mod, Dwarf_Addr addr, Dwarf_Addr *bias)
{
    auto die = dwfl_module_addrdie(mod, addr, bias);
    if (die)
        return die;

    while ((die = dwfl_module_nextcu(mod, die, bias))) {
        if (find_fundie_by_pc(die, addr - *bias))
            return die;
    }

    return nullptr;
}

int PerfSymbolTable::lookupFrame(Dwarf_Addr ip, bool isKernel,
                                 bool *isInterworking)
{
    auto it = m_addressCache.constFind(ip);
    if (it != m_addressCache.constEnd()) {
        *isInterworking = it->isInterworking;
        return it->locationId;
    }

    qint32 binaryId = -1;
    quint64 elfStart = 0;

    const auto& elf = findElf(ip);
    if (elf.isValid()) {
        binaryId = m_unwind->resolveString(elf.originalFileName);
        elfStart = elf.addr;
    }

    Dwfl_Module *mod = module(ip);

    PerfUnwind::Location addressLocation(
                (m_unwind->architecture() != PerfRegisterInfo::ARCH_ARM || (ip & 1))
                ? ip : ip + 1, -1, m_pid);
    PerfUnwind::Location functionLocation(addressLocation);

    QByteArray symname;
    GElf_Sym sym;
    GElf_Off off = 0;

    if (mod) {
        // For addrinfo we need the raw pointer into symtab, so we need to adjust ourselves.
        symname = dwfl_module_addrinfo(mod, addressLocation.address, &off, &sym, 0, 0, 0);

        if (off == addressLocation.address) {// no symbol found
            functionLocation.address = elfStart; // use the start of the elf as "function"
            addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);
        } else {
            Dwarf_Addr bias = 0;
            functionLocation.address -= off; // in case we don't find anything better
            Dwarf_Die *die = find_die(mod, addressLocation.address, &bias);

            if (die) {
                auto srcloc = dwarf_getsrc_die(die, addressLocation.address - bias);
                if (srcloc) {
                    const char* srcfile = dwarf_linesrc(srcloc, nullptr, nullptr);
                    int line, column;
                    dwarf_lineno(srcloc, &line);
                    dwarf_linecol(srcloc, &column);
                    if (srcfile) {
                        const QByteArray file = srcfile;
                        addressLocation.file = m_unwind->resolveString(file);
                        addressLocation.line = line;
                        addressLocation.column = column;
                    }
                }
            }

            Dwarf_Die *scopes = NULL;
            int nscopes = dwarf_getscopes(die, addressLocation.address - bias, &scopes);

            for (int i = 0; i < nscopes; ++i) {
                Dwarf_Die *scope = &scopes[i];
                const int tag = dwarf_tag(scope);
                if (tag == DW_TAG_subprogram || tag == DW_TAG_inlined_subroutine) {
                    Dwarf_Addr entry = 0;
                    dwarf_entrypc(scope, &entry);
                    functionLocation.address = entry + bias;
                    functionLocation.file = m_unwind->resolveString(dwarf_decl_file(scope));
                    dwarf_decl_line(scope, &functionLocation.line);
                    dwarf_decl_column(scope, &functionLocation.column);
                    break;
                }
            }
            eu_compat_free(scopes);

            addressLocation.parentLocationId = m_unwind->lookupLocation(functionLocation);
            if (die && !m_unwind->hasSymbol(addressLocation.parentLocationId))
                parseDwarf(die, bias, binaryId, isKernel);
            if (addressLocation.parentLocationId == -1)
                addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);
        }
        if (!m_unwind->hasSymbol(addressLocation.parentLocationId)) {
            // no sufficient debug information. Use what we already know
            qint32 symId = m_unwind->resolveString(demangle(symname));
            m_unwind->resolveSymbol(addressLocation.parentLocationId,
                                    PerfUnwind::Symbol(symId, binaryId, isKernel));
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
                                    PerfUnwind::Symbol(symId, binaryId, isKernel));
        }
    }

    Q_ASSERT(addressLocation.parentLocationId != -1);
    Q_ASSERT(m_unwind->hasSymbol(addressLocation.parentLocationId));

    int locationId = m_unwind->resolveLocation(addressLocation);
    *isInterworking = (symname == "$a" || symname == "$t");
    m_addressCache.insert(ip, {locationId, *isInterworking});
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

    if (!dwfl_attach_state(m_dwfl, m_firstElf, m_pid, &threadCallbacks, arg)) {
        qWarning() << m_pid << "failed to attach state" << dwfl_errmsg(dwfl_errno());
        return nullptr;
    }

    return m_dwfl;
}

void PerfSymbolTable::clearCache()
{
    m_addressCache.clear();
    m_perfMap.clear();
    if (m_perfMapFile.isOpen())
        m_perfMapFile.reset();

    // Throw out the dwfl state
    dwfl_end(m_dwfl);
    m_dwfl = dwfl_begin(m_callbacks);

    m_cacheIsDirty = false;
}
