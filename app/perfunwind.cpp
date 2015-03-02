/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd
** All rights reserved.
** For any questions to The Qt Company, please use contact form at http://www.qt.io/contact-us
**
** This file is part of the Qt Enterprise Perf Profiler Add-on.
**
** GNU General Public License Usage
** This file may be used under the terms of the GNU General Public License
** version 3 as published by the Free Software Foundation and appearing in
** the file LICENSE.GPLv3 included in the packaging of this file. Please
** review the following information to ensure the GNU General Public License
** requirements will be met: https://www.gnu.org/licenses/gpl.html.
**
** If you have questions regarding the use of this file, please use
** contact form at http://www.qt.io/contact-us
**
****************************************************************************/

#include "perfunwind.h"
#include "perfregisterinfo.h"

#include <QDir>
#include <QDebug>

#include <limits>
#include <cxxabi.h>

PerfUnwind::PerfUnwind(QIODevice *output, const QString &systemRoot, const QString &debugPath,
                       const QString &extraLibsPath, const QString &appPath) :
    output(output), lastPid(0), registerArch(PerfRegisterInfo::ARCH_INVALID),
    systemRoot(systemRoot), extraLibsPath(extraLibsPath), appPath(appPath)
{
    currentUnwind.unwind = this;
    offlineCallbacks.find_elf = dwfl_build_id_find_elf;
    offlineCallbacks.find_debuginfo =  dwfl_standard_find_debuginfo;
    offlineCallbacks.section_address = dwfl_offline_section_address;
    QByteArray newDebugInfo = (":" + debugPath + ":" + appPath + ":" + extraLibsPath + ":" +
                               systemRoot).toUtf8();
    debugInfoPath = new char[newDebugInfo.length() + 1];
    debugInfoPath[newDebugInfo.length()] = 0;
    memcpy(debugInfoPath, newDebugInfo.data(), newDebugInfo.length());
    offlineCallbacks.debuginfo_path = &debugInfoPath;
    dwfl = dwfl_begin(&offlineCallbacks);
}

PerfUnwind::~PerfUnwind()
{
    delete[] debugInfoPath;
}

bool findInExtraPath(QFileInfo &path, const QString &fileName)
{
    path.setFile(path.absoluteFilePath() + "/" + fileName);
    if (path.exists())
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

void PerfUnwind::registerElf(const PerfRecordMmap &mmap)
{
    QMap<quint64, ElfInfo> &procElfs = elfs[mmap.pid()];

    // No point in mapping the same file twice
    if (procElfs.find(mmap.addr()) != procElfs.end())
        return;

    QString fileName = QFileInfo(mmap.filename()).fileName();
    QFileInfo path;
    if (mmap.pid() != s_kernelPid) {
        path.setFile(appPath + "/" + fileName);
        if (!path.isFile()) {
            path.setFile(extraLibsPath);
            if (!findInExtraPath(path, fileName))
                path.setFile(systemRoot + mmap.filename());
        }
    } else { // kernel
        path.setFile(systemRoot + mmap.filename());
    }

    if (path.isFile())
        procElfs[mmap.addr()] = ElfInfo(path, mmap.len());
    else
        qWarning() << "cannot find file to report for" << QString::fromLocal8Bit(mmap.filename());

}

void PerfUnwind::registerThread(quint32 tid, const QString &name)
{
    threads[tid] = name;
}

Dwfl_Module *PerfUnwind::reportElf(quint64 ip, quint32 pid) const
{
    QHash<quint32, QMap<quint64, ElfInfo> >::ConstIterator elfsIt = elfs.find(pid);
    if (elfsIt == elfs.end()) {
        qWarning() << "Process" << pid << "has no elfs";
        return 0;
    }
    const QMap<quint64, ElfInfo> &procElfs = elfsIt.value();
    QMap<quint64, ElfInfo>::ConstIterator i = procElfs.upperBound(ip);
    if (i == procElfs.end() || i.key() != ip) {
        if (i != procElfs.begin())
            --i;
        else
            i = procElfs.end();
    }

//    /* On ARM, symbols for thumb functions have 1 added to
//     * the symbol address as a flag - remove it */
//    if ((ehdr.e_machine == EM_ARM) &&
//        (map->type == MAP__FUNCTION) &&
//        (sym.st_value & 1))
//        --sym.st_value;
//
//    ^ We don't have to do this here as libdw is supposed to handle it from version 0.160.

    if (i != procElfs.end() && i.key() + i.value().length > ip) {
        Dwfl_Module *ret = dwfl_report_elf(
                    dwfl, i.value().file.fileName().toLocal8Bit().constData(),
                    i.value().file.absoluteFilePath().toLocal8Bit().constData(), -1, i.key(),
                    false);
        if (!ret)
            qWarning() << "failed to report" << i.value().file.absoluteFilePath() << "for"
                       << QString("0x%1").arg(i.key(), 0, 16).toLocal8Bit().constData() << ":"
                       << dwfl_errmsg(dwfl_errno());
        return ret;
    } else {
        qWarning() << "no elf found for IP"
                   << QString("0x%1").arg(ip, 0, 16).toLocal8Bit().constData();
        return 0;
    }
}

QDataStream &operator<<(QDataStream &stream, const PerfUnwind::Frame &frame)
{
    return stream << frame.frame << frame.isKernel << frame.symbol << frame.elfFile
                  << frame.srcFile << frame.line << frame.column;
}

static pid_t nextThread(Dwfl *dwfl, void *arg, void **threadArg)
{
    /* Stop after first thread. */
    if (*threadArg != 0)
        return 0;

    *threadArg = arg;
    return dwfl_pid(dwfl);
}


static bool memoryRead(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg)
{
    Q_UNUSED(dwfl)
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);

    /* Check overflow. */
    if (addr + sizeof(Dwarf_Word) < addr) {
        qWarning() << "Invalid memory read requested by dwfl" << addr;
        ui->broken = true;
        return false;
    }

    const QByteArray &stack = ui->sample->userStack();

    quint64 start = ui->sample->registerValue(PerfRegisterInfo::s_perfSp[ui->unwind->architecture()]);
    quint64 end = start + stack.size();

    if (addr < start || addr + sizeof(Dwarf_Word) > end) {
        qWarning() << "Cannot read memory at" << addr;
        qWarning() << "dwfl should only read stack state (" << start << "to" << end
                   << ") with memoryRead().";
        ui->broken = true;
        return false;
    }

    *result = *(Dwarf_Word *)(&stack.data()[addr - start]);
    return true;
}

bool setInitialRegisters(Dwfl_Thread *thread, void *arg)
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

    return dwfl_thread_state_registers(thread, 0, numRegs, dwarfRegs);
}

static const Dwfl_Thread_Callbacks callbacks = {
    nextThread, NULL, memoryRead, setInitialRegisters, NULL, NULL
};

static PerfUnwind::Frame lookupSymbol(PerfUnwind::UnwindInfo *ui, Dwfl *dwfl, Dwarf_Addr ip,
                                      bool isKernel)
{
    Dwfl_Module *mod = dwfl_addrmodule (dwfl, ip);
    const char *symname = NULL;
    if (!mod)
        mod = ui->unwind->reportElf(ip, isKernel ? PerfUnwind::s_kernelPid : ui->unwind->pid());

    const char *elfFile = NULL;
    const char *srcFile = NULL;
    int line = 0;
    int column = 0;
    GElf_Sym sym;
    GElf_Off off;

    bool do_adjust = (ui->unwind->architecture() == PerfRegisterInfo::ARCH_ARM);
    if (mod) {
        Dwarf_Addr adjusted = (!do_adjust || (ip & 1)) ? ip : ip + 1;
        // For addrinfo we need the raw pointer into symtab, so we need to adjust ourselves.
        symname = dwfl_module_addrinfo(mod, adjusted, &off, &sym, 0,
                                       0, 0);
        elfFile = dwfl_module_info(mod, 0, 0, 0, 0, 0, 0, 0);

        // We take the first line of the function for now, in order to reduce UI complexity
        Dwfl_Line *srcLine = dwfl_module_getsrc(mod, adjusted);
        if (srcLine)
            srcFile = dwfl_lineinfo(srcLine, NULL, &line, &column, NULL, NULL);
    }

    if (symname) {
        char *demangled = NULL;
        int status = -1;
        if (symname[0] == '_' && symname[1] == 'Z') {
            demangled = abi::__cxa_demangle (symname, 0, 0, &status);
        }

        // Adjust it back. The symtab entries are 1 off for all practical purposes.
        return PerfUnwind::Frame((do_adjust && (sym.st_value & 1)) ? sym.st_value - 1 :
                                                                     sym.st_value,
                                 isKernel, status == 0 ? demangled : symname, elfFile, srcFile,
                                 line, column);
        free(demangled);
    } else {
        qWarning() << "no symbol found for" << ip << "in" << elfFile;
        ui->broken = !isKernel;
        return PerfUnwind::Frame(ip, isKernel, symname, elfFile, srcFile, line, column);
    }
}

static int frameCallback(Dwfl_Frame *state, void *arg)
{
    Dwarf_Addr pc;
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);

    bool isactivation;
    if (!dwfl_frame_pc(state, &pc, &isactivation)) {
        qWarning() << dwfl_errmsg(dwfl_errno());
        ui->broken = true;
        return DWARF_CB_ABORT;
    }

    Dwarf_Addr pc_adjusted = pc - (isactivation ? 0 : 1);

    /* Get PC->SYMNAME.  */
    Dwfl_Thread *thread = dwfl_frame_thread (state);
    Dwfl *dwfl = dwfl_thread_dwfl (thread);

    // isKernel = false as unwinding generally only works on user code
    ui->frames.append(lookupSymbol(ui, dwfl, pc_adjusted, false));
    return DWARF_CB_OK;
}

void PerfUnwind::unwindStack()
{
    if (dwfl_getthread_frames(dwfl, currentUnwind.sample->pid(), frameCallback, &currentUnwind))
        qWarning() << "failed to get some frames:" << currentUnwind.sample->tid()
                   << dwfl_errmsg(dwfl_errno());
}

void PerfUnwind::resolveCallchain()
{
    bool isKernel = false;
    for (int i = 0; i < currentUnwind.sample->callchain().length(); ++i) {
        quint64 ip = currentUnwind.sample->callchain()[i];
        if (ip > PERF_CONTEXT_MAX) {
            switch (ip) {
            case PERF_CONTEXT_HV: // hypervisor
            case PERF_CONTEXT_KERNEL:
                isKernel = true;
                break;
            case PERF_CONTEXT_USER:
                isKernel = false;
                break;
            default:
                qWarning() << "invalid callchain context" << ip;
                return;
            }
        }

        // sometimes it skips the first user frame.
        if (i == 0 && !isKernel && ip != currentUnwind.sample->ip())
            currentUnwind.frames.append(lookupSymbol(&currentUnwind, dwfl,
                                                     currentUnwind.sample->ip(), false));

        if (ip <= PERF_CONTEXT_MAX)
            currentUnwind.frames.append(lookupSymbol(&currentUnwind, dwfl, ip, isKernel));
    }
}

void sendBuffer(QIODevice *output, const QByteArray &buffer)
{
    quint32 size = buffer.length();
    output->write(reinterpret_cast<char *>(&size), sizeof(quint32));
    output->write(buffer);
}

void PerfUnwind::analyze(const PerfRecordSample &sample)
{
    if (sample.pid() != lastPid) {
        dwfl_end(dwfl);
        dwfl = dwfl_begin(&offlineCallbacks);
        lastPid = sample.pid();
        reportElf(sample.ip(), lastPid);

        if (!dwfl) {
            qWarning() << "failed to initialize dwfl" << dwfl_errmsg(dwfl_errno());
            lastPid = -1;
            return;
        }
        if (!dwfl_attach_state(dwfl, 0, sample.pid(), &callbacks, &currentUnwind)) {
            qWarning() << "failed to attach state:" << dwfl_errmsg(dwfl_errno());
            lastPid = -1;
            return;
        }
    } else {
        if (!dwfl_addrmodule (dwfl, sample.ip()))
            reportElf(sample.ip(), lastPid);
    }

    currentUnwind.broken = false;
    currentUnwind.sample = &sample;
    currentUnwind.frames.clear();
    if (sample.callchain().length() > 0)
        resolveCallchain();
    if (sample.registerAbi() != 0 && sample.userStack().length() > 0)
        unwindStack();

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly)
            << static_cast<quint8>(currentUnwind.broken ? BadStack : GoodStack) << lastPid
            << sample.tid() << threads[sample.tid()] << sample.time() << currentUnwind.frames;
    sendBuffer(output, buffer);
}

void PerfUnwind::fork(const PerfRecordFork &sample)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(ThreadStart)
                                               << sample.childPid() << sample.childTid()
                                               << threads[sample.childTid()] << sample.time()
                                               << QVector<PerfUnwind::Frame>();
    sendBuffer(output, buffer);
}

void PerfUnwind::exit(const PerfRecordExit &sample)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(ThreadEnd)
                                               << sample.childPid() << sample.childTid()
                                               << threads[sample.childTid()] << sample.time()
                                               << QVector<PerfUnwind::Frame>();
    sendBuffer(output, buffer);
}
