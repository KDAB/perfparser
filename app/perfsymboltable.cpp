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

#include <QDir>
#include <QDebug>

#include <cstring>
#include <cxxabi.h>

PerfSymbolTable::PerfSymbolTable(quint32 pid, Dwfl_Callbacks *callbacks, PerfUnwind *parent) :
    m_perfMapFile(QString::fromLatin1("/tmp/perf-%1.map").arg(pid)),
    m_unwind(parent), m_callbacks(callbacks)
{
    m_dwfl = dwfl_begin(m_callbacks);
}

PerfSymbolTable::~PerfSymbolTable()
{
    dwfl_end(m_dwfl);
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
        if (!mod) {
            mod = ui->unwind->reportElf(addr, PerfUnwind::s_kernelPid);
            if (!mod)
                return false;
        }
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
        ui->broken = true;
        return false;
    }

    const QByteArray &stack = ui->sample->userStack();

    quint64 start = ui->sample->registerValue(
                PerfRegisterInfo::s_perfSp[ui->unwind->architecture()]);
    quint64 end = start + stack.size();

    if (addr < start || addr + sizeof(Dwarf_Word) > end) {
        // not stack, try reading from ELF
        if (!accessDsoMem(dwfl, ui, addr, result)) {
            ui->broken = true;
            return false;
        }
    } else {
        std::memcpy(result, &(stack.data()[addr - start]), sizeof(Dwarf_Word));
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
    if (ui->isInterworking()) {
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
    else if (!path.isDir())
        return false;

    QDir absDir = path.absoluteDir();
    foreach (const QString &entry, absDir.entryList(QStringList(),
                                                    QDir::Dirs | QDir::NoDotAndDotDot)) {
        path.setFile(absDir, entry);
        if (findInExtraPath(path, fileName))
            return true;
    }
    return false;
}

void PerfSymbolTable::registerElf(const PerfRecordMmap &mmap, const QString &appPath,
                                  const QString &systemRoot, const QString &extraLibsPath)
{
    auto i = m_elfs.upperBound(mmap.addr());
    if (i != m_elfs.begin())
        --i;

    bool cacheInvalid = false;
    QMap<quint64, ElfInfo> toBeInserted;
    while (i != m_elfs.end() && i.key() < mmap.addr() + mmap.len()) {
        if (i.key() + i.value().length <= mmap.addr()) {
            ++i;
            continue;
        }

        if (i.key() + i.value().length > mmap.addr() + mmap.len()) {
            // Move or copy the original mmap to the part after the new mmap. The new length is the
            // difference between the end points (begin + length) of the two. The original mmap
            // is either removed or shortened by the following if/else construct.
            toBeInserted.insert(mmap.addr() + mmap.len(),
                                ElfInfo(i.value().file,
                                        i.key() + i.value().length - mmap.addr() - mmap.len()));
        }

        if (i.key() >= mmap.addr()) {
            i = m_elfs.erase(i);
        } else {
            i.value().length = mmap.addr() - i.key();
            ++i;
        }

        // Overlapping module. Clear the cache
        cacheInvalid = true;
    }
    m_elfs.unite(toBeInserted);
    if (cacheInvalid)
        clearCache();

    QLatin1String filePath(mmap.filename());
    QFileInfo fileInfo(filePath);
    QFileInfo fullPath;
    if (mmap.pid() != PerfUnwind::s_kernelPid) {
        fullPath.setFile(appPath);
        if (!findInExtraPath(fullPath, fileInfo.fileName())) {
            bool found = false;
            foreach (const QString &extraPath, extraLibsPath.split(QLatin1Char(':'))) {
                fullPath.setFile(extraPath);
                if (findInExtraPath(fullPath, fileInfo.fileName())) {
                    found = true;
                    break;
                }
            }
            if (!found)
                fullPath.setFile(systemRoot + filePath);
        }
    } else { // kernel
        fullPath.setFile(systemRoot + filePath);
    }

    m_elfs[mmap.addr()] = ElfInfo(fullPath, mmap.len(), fullPath.isFile());
}

Dwfl_Module *PerfSymbolTable::reportElf(quint64 ip, const PerfSymbolTable::ElfInfo **info)
{
    QMap<quint64, ElfInfo>::ConstIterator i = m_elfs.upperBound(ip);
    if (i == m_elfs.end() || i.key() != ip) {
        if (i != m_elfs.begin())
            --i;
        else
            i = m_elfs.end();
    }

//    /* On ARM, symbols for thumb functions have 1 added to
//     * the symbol address as a flag - remove it */
//    if ((ehdr.e_machine == EM_ARM) &&
//        (map->type == MAP__FUNCTION) &&
//        (sym.st_value & 1))
//        --sym.st_value;
//
//    ^ We don't have to do this here as libdw is supposed to handle it from version 0.160.

    if (i != m_elfs.end() && i.key() + i.value().length > ip) {
        if (info)
            *info = &i.value();
        if (!i.value().found)
            return 0;
        Dwfl_Module *ret = dwfl_report_elf(
                    m_dwfl, i.value().file.fileName().toLocal8Bit().constData(),
                    i.value().file.absoluteFilePath().toLocal8Bit().constData(), -1, i.key(),
                    false);
        if (!ret)
            qWarning() << "failed to report" << i.value().file.absoluteFilePath() << "for"
                       << QString::fromLatin1("0x%1").arg(i.key(), 0, 16).toLocal8Bit().constData()
                       << ":" << dwfl_errmsg(dwfl_errno());
        return ret;
    } else {
        return 0;
    }
}

PerfUnwind::Frame PerfSymbolTable::lookupFrame(Dwarf_Addr ip, bool isKernel)
{
    Dwfl_Module *mod = m_dwfl ? dwfl_addrmodule(m_dwfl, ip) : 0;
    QByteArray elfFile;

    if (m_dwfl && !mod) {
        const ElfInfo *elfInfo = 0;
        mod = reportElf(ip, &elfInfo);
        if (!mod && elfInfo)
            elfFile = elfInfo->file.fileName().toLocal8Bit();
    }

    Dwarf_Addr adjusted = (m_unwind->architecture() != PerfRegisterInfo::ARCH_ARM
            || (ip & 1)) ? ip : ip + 1;
    if (mod)
        elfFile = dwfl_module_info(mod, 0, 0, 0, 0, 0, 0, 0);

    auto it = m_addrCache.constFind(ip);
    // Check for elfFile as it might have loaded a different file to the same address in the mean
    // time. We don't consider the case of loading the same file again at a different, overlapping
    // offset.
    if (it != m_addrCache.constEnd() && it->elfFile == elfFile)
        return *it;

    QByteArray symname;
    QByteArray srcFile;
    int line = 0;
    int column = 0;
    GElf_Sym sym;
    GElf_Off off = 0;

    if (mod) {
        // For addrinfo we need the raw pointer into symtab, so we need to adjust ourselves.
        symname = dwfl_module_addrinfo(mod, adjusted, &off, &sym, 0, 0, 0);
        if (off == adjusted) // no symbol found
            off = 0;
        else if (m_unwind->granularity() == PerfUnwind::Function)
            adjusted -= off;

        Dwfl_Line *srcLine = dwfl_module_getsrc(mod, adjusted);
        if (srcLine)
            srcFile = dwfl_lineinfo(srcLine, NULL, &line, &column, NULL, NULL);
    }

    if (!symname.isEmpty()) {
        char *demangled = NULL;
        int status = -1;
        bool isInterworking = false;
        if (symname[0] == '_' && symname[1] == 'Z')
            demangled = abi::__cxa_demangle(symname, 0, 0, &status);
        else if (m_unwind->architecture() == PerfRegisterInfo::ARCH_ARM && symname[0] == '$'
                 && (symname[1] == 'a' || symname[1] == 't') && symname[2] == '\0')
            isInterworking = true;

        // Adjust it back. The symtab entries are 1 off for all practical purposes.
        PerfUnwind::Frame frame(adjusted, isKernel, status == 0 ? QByteArray(demangled) : symname,
                                elfFile, srcFile, line, column, isInterworking);
        free(demangled);
        m_addrCache.insert(ip, frame);
        return frame;
    } else {
        symname = symbolFromPerfMap(adjusted, &off);
        if (m_unwind->granularity() == PerfUnwind::Function)
            adjusted -= off;
        PerfUnwind::Frame frame(adjusted, isKernel, symname, elfFile, srcFile, line, column);
        m_addrCache.insert(ip, frame);
        return frame;
    }
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
    if (m_elfs.isEmpty())
        return false;

    const auto last = (--m_elfs.constEnd());
    const auto first = m_elfs.constBegin();
    return first.key() <= address && last.key() + last.value().length > address;
}

Dwfl *PerfSymbolTable::attachDwfl(quint32 pid, void *arg)
{
    // Already attached, nothing to do
    if (static_cast<pid_t>(pid) == dwfl_pid(m_dwfl))
        return m_dwfl;

    // Report some random elf, so that dwfl guesses the target architecture.
    for (auto it = m_elfs.constBegin(), end = m_elfs.constEnd(); it != end; ++it) {
        if (!it->found)
            continue;
        if (dwfl_report_elf(m_dwfl, it.value().file.fileName().toLocal8Bit().constData(),
                            it.value().file.absoluteFilePath().toLocal8Bit().constData(), -1,
                            it.key(), false)) {
            break;
        }
    }

    if (!dwfl_attach_state(m_dwfl, 0, pid, &threadCallbacks, arg)) {
        qWarning() << "failed to attach state" << dwfl_errmsg(dwfl_errno());
        return nullptr;
    }

    return m_dwfl;
}

void PerfSymbolTable::clearCache()
{
    m_addrCache.clear();
    m_perfMap.clear();
    if (m_perfMapFile.isOpen())
        m_perfMapFile.reset();

    // Throw out the dwfl state
    dwfl_end(m_dwfl);
    m_dwfl = dwfl_begin(m_callbacks);
}
