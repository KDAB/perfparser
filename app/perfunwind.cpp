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
#include <cstring>
#include <cxxabi.h>

static const QChar colon = QLatin1Char(':');

PerfUnwind::PerfUnwind(QIODevice *output, const QString &systemRoot, const QString &debugPath,
                       const QString &extraLibsPath, const QString &appPath) :
    output(output), registerArch(PerfRegisterInfo::ARCH_INVALID), systemRoot(systemRoot),
    extraLibsPath(extraLibsPath), appPath(appPath), sampleBufferSize(0), sampleGranularity(Function)
{
    currentUnwind.unwind = this;
    offlineCallbacks.find_elf = dwfl_build_id_find_elf;
    offlineCallbacks.find_debuginfo =  dwfl_standard_find_debuginfo;
    offlineCallbacks.section_address = dwfl_offline_section_address;
    QByteArray newDebugInfo = (colon + debugPath + colon + appPath + colon + extraLibsPath + colon +
                               systemRoot).toUtf8();
    debugInfoPath = new char[newDebugInfo.length() + 1];
    debugInfoPath[newDebugInfo.length()] = 0;
    std::memcpy(debugInfoPath, newDebugInfo.data(), newDebugInfo.length());
    offlineCallbacks.debuginfo_path = &debugInfoPath;
    dwfl = dwfl_begin(&offlineCallbacks);
}

PerfUnwind::~PerfUnwind()
{
    foreach (const PerfRecordSample &sample, sampleBuffer)
        analyze(sample);

    delete[] debugInfoPath;
    dwfl_end(dwfl);
}

bool findInExtraPath(QFileInfo &path, const QString &fileName)
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

void PerfUnwind::registerElf(const PerfRecordMmap &mmap)
{
    QMap<quint64, ElfInfo> &procElfs = elfs[mmap.pid()];

    auto i = procElfs.upperBound(mmap.addr());
    if (i != procElfs.begin())
        --i;
    while (i != procElfs.end() && i.key() < mmap.addr() + mmap.len()) {
        if (i.key() + i.value().length <= mmap.addr()) {
            ++i;
            continue;
        }

        if (i.key() + i.value().length > mmap.addr() + mmap.len()) {
            // Move or copy the original mmap to the part after the new mmap. The new length is the
            // difference between the end points (begin + length) of the two. The original mmap
            // is either removed or shortened by the following if/else construct.
            procElfs.insert(mmap.addr() + mmap.len(),
                            ElfInfo(i.value().file,
                                    i.key() + i.value().length - mmap.addr() - mmap.len()));
        }

        if (i.key() >= mmap.addr()) {
            i = procElfs.erase(i);
        } else {
            i.value().length = mmap.addr() - i.key();
            ++i;
        }

        if (dwfl && dwfl_pid(dwfl) == static_cast<pid_t>(mmap.pid())) {
            dwfl_end(dwfl); // Throw out the dwfl state
            dwfl = 0;
        }
    }

    QLatin1String filePath(mmap.filename());
    QFileInfo fileInfo(filePath);
    QFileInfo fullPath;
    if (mmap.pid() != s_kernelPid) {
        fullPath.setFile(appPath);
        if (!findInExtraPath(fullPath, fileInfo.fileName())) {
            bool found = false;
            foreach (const QString &extraPath, extraLibsPath.split(colon)) {
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

    if (fullPath.isFile())
        procElfs[mmap.addr()] = ElfInfo(fullPath, mmap.len());
    else
        procElfs[mmap.addr()] = ElfInfo(fileInfo, mmap.len(), false);
}

void sendBuffer(QIODevice *output, const QByteArray &buffer)
{
    quint32 size = buffer.length();
    output->write(reinterpret_cast<char *>(&size), sizeof(quint32));
    output->write(buffer);
}

void PerfUnwind::comm(PerfRecordComm &comm)
{

    threads[comm.tid()] = QString::fromLocal8Bit(comm.comm());
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(Command)
                                               << comm.pid() << comm.tid() << threads[comm.tid()]
                                               << comm.time() << QVector<PerfUnwind::Frame>();
    sendBuffer(output, buffer);
}

Dwfl_Module *PerfUnwind::reportElf(quint64 ip, quint32 pid, const ElfInfo **info) const
{
    QHash<quint32, QMap<quint64, ElfInfo> >::ConstIterator elfsIt = elfs.find(pid);
    if (elfsIt == elfs.end())
        return 0;

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
        if (info)
            *info = &i.value();
        if (!i.value().found)
            return 0;
        Dwfl_Module *ret = dwfl_report_elf(
                    dwfl, i.value().file.fileName().toLocal8Bit().constData(),
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

bool PerfUnwind::ipIsInKernelSpace(quint64 ip) const
{
    auto elfsIt = elfs.constFind(quint32(s_kernelPid));
    if (elfsIt == elfs.constEnd())
        return false;

    const QMap<quint64, ElfInfo> &kernelElfs = elfsIt.value();
    if (kernelElfs.isEmpty())
        return false;

    const auto last = (--kernelElfs.constEnd());
    const auto first = kernelElfs.constBegin();
    return first.key() <= ip && last.key() + last.value().length > ip;
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

    quint64 start = ui->sample->registerValue(PerfRegisterInfo::s_perfSp[ui->unwind->architecture()]);
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

    if (ui->isInterworking) // Go one frame up to get the rest of the stack at interworking veneers.
        dwarfRegs[PerfRegisterInfo::s_dwarfIp[architecture][abi]] =
                dwarfRegs[PerfRegisterInfo::s_dwarfLr[architecture][abi]];

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

static const Dwfl_Thread_Callbacks callbacks = {
    nextThread, NULL, memoryRead, setInitialRegisters, NULL, NULL
};

PerfUnwind::Frame PerfUnwind::lookupSymbol(Dwarf_Addr ip, bool isKernel)
{
    quint32 pid = currentUnwind.sample->pid();
    Dwfl_Module *mod = dwfl ? dwfl_addrmodule(dwfl, ip) : 0;
    QByteArray elfFile;

    if (dwfl && !mod) {
        const ElfInfo *elfInfo = 0;
        mod = reportElf(ip, isKernel ? s_kernelPid : pid, &elfInfo);
        if (!mod && elfInfo)
            elfFile = elfInfo->file.fileName().toLocal8Bit();
    }

    Dwarf_Addr adjusted = (architecture() != PerfRegisterInfo::ARCH_ARM || (ip & 1)) ? ip : ip + 1;
    if (mod)
        elfFile = dwfl_module_info(mod, 0, 0, 0, 0, 0, 0, 0);

    QHash<Dwarf_Addr, Frame> &processAddrCache = addrCache[pid];
    auto it = processAddrCache.constFind(ip);
    // Check for elfFile as it might have loaded a different file to the same address in the mean
    // time. We don't consider the case of loading the same file again at a different, overlapping
    // offset.
    if (it != processAddrCache.constEnd() && it->elfFile == elfFile)
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
        else if (granularity() == Function)
            adjusted -= off;

        Dwfl_Line *srcLine = dwfl_module_getsrc(mod, adjusted);
        if (srcLine)
            srcFile = dwfl_lineinfo(srcLine, NULL, &line, &column, NULL, NULL);
    }

    if (!symname.isEmpty()) {
        char *demangled = NULL;
        int status = -1;
        if (symname[0] == '_' && symname[1] == 'Z')
            demangled = abi::__cxa_demangle(symname, 0, 0, &status);
        else if (architecture() == PerfRegisterInfo::ARCH_ARM && symname[0] == '$'
                 && (symname[1] == 'a' || symname[1] == 't') && symname[2] == '\0')
            currentUnwind.isInterworking = true;

        // Adjust it back. The symtab entries are 1 off for all practical purposes.
        Frame frame(adjusted, isKernel, status == 0 ? QByteArray(demangled) : symname, elfFile,
                    srcFile, line, column);
        free(demangled);
        processAddrCache.insert(ip, frame);
        return frame;
    } else {
        symname = symbolFromPerfMap(adjusted, pid, &off);
        if (granularity() == Function)
            adjusted -= off;
        Frame frame(adjusted, isKernel, symname, elfFile, srcFile, line, column);
        processAddrCache.insert(ip, frame);
        return frame;
    }
}

static int frameCallback(Dwfl_Frame *state, void *arg)
{
    Dwarf_Addr pc;
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);

    bool isactivation;
    if (!dwfl_frame_pc(state, &pc, &isactivation) ||
            ui->frames.length() > PerfUnwind::s_maxFrames) {
        ui->broken = true;
        qWarning() << dwfl_errmsg(dwfl_errno()) << ui->broken;
        return DWARF_CB_ABORT;
    }

    Dwarf_Addr pc_adjusted = pc - (isactivation ? 0 : 1);

    // isKernel = false as unwinding generally only works on user code
    ui->frames.append(ui->unwind->lookupSymbol(pc_adjusted, false));
    return DWARF_CB_OK;
}

void PerfUnwind::unwindStack()
{
    dwfl_getthread_frames(dwfl, currentUnwind.sample->pid(), frameCallback, &currentUnwind);
    if (currentUnwind.isInterworking && currentUnwind.frames.length() == 1) {
        // If it's an ARM interworking veneer, we assume that we can find a return address in LR and
        // no stack has been used for the veneer itself.
        // The reasoning is that any symbol jumped to by the veneer has to work with or without
        // using the veneer. It needs a valid return address and when it returns the stack pointer
        // must be the same in both cases. Thus, the veneer cannot touch the stack pointer and there
        // has to be a return address in LR, provided by the caller.
        // So, just try again, and make setInitialRegisters use LR for IP.
        dwfl_getthread_frames(dwfl, currentUnwind.sample->pid(), frameCallback, &currentUnwind);
    }
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
            currentUnwind.frames.append(lookupSymbol(currentUnwind.sample->ip(), false));

        if (ip <= PERF_CONTEXT_MAX)
            currentUnwind.frames.append(lookupSymbol(ip, isKernel));
    }
}

void PerfUnwind::sample(const PerfRecordSample &sample)
{
    sampleBuffer.append(sample);
    sampleBufferSize += sample.size();

    while (sampleBufferSize > maxSampleBufferSize) {
        const PerfRecordSample &sample = sampleBuffer.front();
        sampleBufferSize -= sample.size();
        analyze(sample);
        sampleBuffer.removeFirst();
    }
}

void PerfUnwind::analyze(const PerfRecordSample &sample)
{
    currentUnwind.broken = false;
    currentUnwind.isInterworking = false;
    currentUnwind.sample = &sample;
    currentUnwind.frames.clear();

    if (!dwfl || static_cast<pid_t>(sample.pid()) != dwfl_pid(dwfl)) {
        dwfl_end(dwfl);
        dwfl = dwfl_begin(&offlineCallbacks);
        reportElf(sample.ip(), sample.pid());

        if (!dwfl) {
            qWarning() << "failed to initialize dwfl" << dwfl_errmsg(dwfl_errno());
            currentUnwind.broken = true;
        } else if (!dwfl_attach_state(dwfl, 0, sample.pid(), &callbacks, &currentUnwind)) {
            qWarning() << "failed to attach state" << dwfl_errmsg(dwfl_errno());
            currentUnwind.broken = true;
        }
    } else {
        if (!dwfl_addrmodule (dwfl, sample.ip()))
            reportElf(sample.ip(), sample.pid());
    }
    updatePerfMap(sample.pid());

    if (sample.callchain().length() > 0)
        resolveCallchain();
    if (sample.registerAbi() != 0) {
        if (dwfl && sample.userStack().length() > 0)
            unwindStack();
    }
    // If nothing was found, at least look up the IP
    if (currentUnwind.frames.isEmpty())
        currentUnwind.frames.append(lookupSymbol(sample.ip(), ipIsInKernelSpace(sample.ip())));

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly)
            << static_cast<quint8>(currentUnwind.broken ? BadStack : GoodStack) << sample.pid()
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

bool operator<(const PerfUnwind::PerfMap::Symbol &a, const PerfUnwind::PerfMap::Symbol &b)
{
    return a.start < b.start;
}

QByteArray PerfUnwind::symbolFromPerfMap(quint64 ip, quint32 pid, GElf_Off *offset) const
{
    const PerfMap &map = perfMaps[pid];

    QVector<PerfMap::Symbol>::ConstIterator sym =
            std::upper_bound(map.symbols.begin(), map.symbols.end(), PerfMap::Symbol(ip));
    if (sym == map.symbols.begin())
        return QByteArray();
    --sym;

    if (sym != map.symbols.end() && sym->start <= ip && sym->start + sym->length > ip) {
        *offset = ip - sym->start;
        return sym->name;
    }
    *offset = 0;
    return QByteArray();
}

void PerfUnwind::updatePerfMap(quint32 pid)
{
    PerfMap &map = perfMaps[pid];
    if (!map.file) {
        map.file.reset(new QFile(QString::fromLatin1("/tmp/perf-%1.map").arg(pid)));
        map.file->open(QIODevice::ReadOnly);
    }

    bool readLine = false;
    while (!map.file->atEnd()) {
        QByteArrayList line = map.file->readLine().split(' ');
        if (line.length() >= 3) {
            bool ok = false;
            quint64 start = line.takeFirst().toULongLong(&ok, 16);
            if (!ok)
                continue;
            quint64 length = line.takeFirst().toULongLong(&ok, 16);
            if (!ok)
                continue;
            QByteArray name = line.join(' ').trimmed();
            map.symbols.append(PerfMap::Symbol(start, length, name));
            readLine = true;
        }
    }

    if (readLine)
        std::sort(map.symbols.begin(), map.symbols.end());
}
