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
#include <cxxabi.h>

PerfSymbolTable::PerfSymbolTable(quint32 pid, Dwfl_Callbacks *callbacks, PerfUnwind *parent) :
    m_perfMapFile(QString::fromLatin1("/tmp/perf-%1.map").arg(pid)),
    m_unwind(parent), m_firstElf(nullptr), m_callbacks(callbacks), m_pid(pid)
{
    m_dwfl = dwfl_begin(m_callbacks);
}

PerfSymbolTable::~PerfSymbolTable()
{
    dwfl_end(m_dwfl);
    elf_end(m_firstElf);
}

static pid_t nextThread(Dwfl *dwfl, void *arg, void **threadArg)
{
    /* Stop after first thread. */
    if (*threadArg != 0)
        return 0;

    *threadArg = arg;
    return dwfl_pid(dwfl);
}

static bool accessDsoMem(Dwfl *dwfl, const PerfUnwind::UnwindInfo *ui, Dwarf_Addr addr,
                         Dwarf_Word *result)
{
    // TODO: Take the pgoff into account? Or does elf_getdata do that already?
    Dwfl_Module *mod = dwfl_addrmodule(dwfl, addr);
    if (!mod) {
        mod = ui->unwind->reportElf(addr, ui->sample->pid());
        if (!mod)
            return false;
    }

    Dwarf_Addr bias;
    Elf_Scn *section = dwfl_module_address_section(mod, &addr, &bias);

    if (section) {
        Elf_Data *data = elf_getdata(section, NULL);
        if (data && data->d_buf && data->d_size > addr) {
            std::memcpy(result, static_cast<char *>(data->d_buf) + addr, sizeof(Dwarf_Word));
            return true;
        }
    }

    return false;
}

static bool memoryRead(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg)
{
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);

    /* Check overflow. */
    if (addr + sizeof(Dwarf_Word) < addr) {
        qWarning() << "Invalid memory read requested by dwfl" << addr;
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
        if (!accessDsoMem(dwfl, ui, addr, result)) {
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
        std::memcpy(result, &(stack.data()[addr - start]), sizeof(Dwarf_Word));
        ui->stackValues[ui->sample->pid()][addr] = *result;
    }
    return true;
}

static bool setInitialRegisters(Dwfl_Thread *thread, void *arg)
{
    const PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);
    quint64 abi = ui->sample->registerAbi() - 1; // ABI 0 means "no registers"
    Q_ASSERT(abi < PerfRegisterInfo::s_numAbis);
    uint architecture = ui->unwind->architecture();
    uint numRegs = PerfRegisterInfo::s_numRegisters[architecture][abi];
    Dwarf_Word dwarfRegs[numRegs];
    for (uint i = 0; i < numRegs; ++i)
        dwarfRegs[i] = ui->sample->registerValue(
                    PerfRegisterInfo::s_perfToDwarf[architecture][abi][i]);

    // Go one frame up to get the rest of the stack at interworking veneers.
    if (ui->isInterworking) {
        dwarfRegs[PerfRegisterInfo::s_dwarfIp[architecture][abi]] =
                dwarfRegs[PerfRegisterInfo::s_dwarfLr[architecture][abi]];
    }

    uint dummyBegin = PerfRegisterInfo::s_dummyRegisters[architecture][0];
    uint dummyNum = PerfRegisterInfo::s_dummyRegisters[architecture][1] - dummyBegin;

    if (dummyNum > 0) {
        Dwarf_Word dummyRegs[dummyNum];
        std::memset(dummyRegs, 0, dummyNum * sizeof(Dwarf_Word));
        if (!dwfl_thread_state_registers(thread, dummyBegin, dummyNum, dummyRegs))
            return false;
    }

    return dwfl_thread_state_registers(thread, 0, numRegs, dwarfRegs);
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
    return path.split(QLatin1Char(':'), QString::SkipEmptyParts);
}

void PerfSymbolTable::registerElf(const PerfRecordMmap &mmap, const QByteArray &buildId,
                                  const QString &appPath, const QString &systemRoot,
                                  const QString &extraLibsPath, const QString &debugInfoPath)
{
    QLatin1String filePath(mmap.filename());
    // special regions, such as [heap], [vdso], [stack], ... as well as //anon
    const bool isSpecialRegion = (mmap.filename().startsWith('[') && mmap.filename().endsWith(']'))
                              || filePath == QLatin1String("//anon");
    QFileInfo fileInfo(filePath);
    QFileInfo fullPath;
    if (isSpecialRegion) {
        // don not set fullPath, these regions don't represent a real file
    } else if (mmap.pid() != PerfUnwind::s_kernelPid) {
        bool found = false;
        // first try to find the debug information via build id, if available
        if (!buildId.isEmpty()) {
            const QString buildIdPath = QString::fromUtf8(mmap.filename() + '/'
                                                            + buildId.toHex());
            foreach (const QString &extraPath, splitPath(debugInfoPath)) {
                fullPath.setFile(extraPath);
                if (findBuildIdPath(fullPath, buildIdPath)) {
                    found = true;
                    break;
                }
            }
        }
        if (!found && !appPath.isEmpty()) {
            // try to find the file in the app path
            fullPath.setFile(appPath);
            found = findInExtraPath(fullPath, fileInfo.fileName());
        }
        if (!found) {
            // try to find the file in the extra libs path
            foreach (const QString &extraPath, splitPath(extraLibsPath)) {
                fullPath.setFile(extraPath);
                if (findInExtraPath(fullPath, fileInfo.fileName())) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            // last fall-back, try the system root
            fullPath.setFile(systemRoot + filePath);
            found = fullPath.exists();
        }

        if (!found) {
            m_unwind->sendError(PerfUnwind::MissingElfFile,
                                PerfUnwind::tr("Could not find ELF file for %1. "
                                               "This can break stack unwinding "
                                               "and lead to missing symbols.").arg(filePath));
        } else if (!m_firstElfFile.isOpen()) {
            m_firstElfFile.setFileName(fullPath.absoluteFilePath());
            if (!m_firstElfFile.open(QIODevice::ReadOnly)) {
                qWarning() << "Failed to open file:" << m_firstElfFile.errorString();
            } else {
                m_firstElf = elf_begin(m_firstElfFile.handle(), ELF_C_READ, nullptr);
                if (!m_firstElf) {
                    qWarning() << "Failed to begin elf:" << elf_errmsg(elf_errno());
                    m_firstElfFile.close();
                } else if (m_firstElf && elf_kind(m_firstElf) == ELF_K_NONE) {
                    // not actually an elf object
                    m_firstElf = nullptr;
                    m_firstElfFile.close();
                }
            }
        }
    } else { // kernel
        fullPath.setFile(systemRoot + filePath);
    }

    bool cacheInvalid = m_elfs.registerElf(mmap.addr(), mmap.len(), mmap.pgoff(), fullPath,
                                           fileInfo.fileName().toUtf8());

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
            char *dsymname = abi::__cxa_demangle(mangledName, demangleBuffer, &demangleBufferLength,
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
    qWarning() << "failed to report" << info.localFile.absoluteFilePath() << "for"
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
    if (!ret)
        reportError(info, dwfl_errmsg(dwfl_errno()));

    return ret;
}

PerfElfMap::ElfInfo PerfSymbolTable::findElf(quint64 ip) const
{
    return m_elfs.findElf(ip);
}

int PerfSymbolTable::lookupFrame(Dwarf_Addr ip, bool isKernel,
                                 bool *isInterworking)
{
    auto it = m_addressCache.constFind(ip);
    if (it != m_addressCache.constEnd()) {
        *isInterworking = it->isInterworking;
        return it->locationId;
    }

    Dwfl_Module *mod = m_dwfl ? dwfl_addrmodule(m_dwfl, ip) : 0;
    qint32 binaryId = -1;

    quint64 elfStart = 0;

    const auto& elf = findElf(ip);
    if (elf.isValid()) {
        binaryId = m_unwind->resolveString(elf.originalFileName);
        elfStart = elf.addr;
        if (m_dwfl) {
            if (mod) {
                // If dwfl has a module and it's not the same as what we want, report the module
                // we want. Many modules overlap ld.so, so if we've reported even one sample from
                // ld.so we would otherwise be blocked from reporting anything that overlaps it.
                Dwarf_Addr mod_start = 0;
                dwfl_module_info(mod, nullptr, &mod_start, nullptr, nullptr, nullptr, nullptr,
                                 nullptr);
                if (elfStart != mod_start)
                    mod = reportElf(elf);
            } else {
                mod = reportElf(elf);
            }
        }
    }

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
        Dwfl_Line *srcLine = dwfl_module_getsrc(mod, addressLocation.address);
        if (srcLine) {
            const QByteArray file = dwfl_lineinfo(srcLine, NULL, &addressLocation.line,
                                                 &addressLocation.column, NULL, NULL);
            addressLocation.file = m_unwind->resolveString(file);
        }

        if (off == addressLocation.address) {// no symbol found
            functionLocation.address = elfStart; // use the start of the elf as "function"
            addressLocation.parentLocationId = m_unwind->resolveLocation(functionLocation);
        } else {
            Dwarf_Addr bias = 0;
            functionLocation.address -= off; // in case we don't find anything better
            Dwarf_Die *die = dwfl_module_addrdie(mod, addressLocation.address, &bias);

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
            free(scopes);

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
}
