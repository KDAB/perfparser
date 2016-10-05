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
#include "perfsymboltable.h"

#include <QDebug>

#include <cstring>

uint qHash(const PerfUnwind::Location &location, uint seed)
{
    QtPrivate::QHashCombine hash;
    seed = hash(seed, location.address);
    seed = hash(seed, location.file);
    seed = hash(seed, location.line);
    seed = hash(seed, location.column);
    return seed;
}

bool operator==(const PerfUnwind::Location &a, const PerfUnwind::Location &b)
{
    return a.address == b.address && a.file == b.file && a.line == b.line && a.column == b.column;
}

PerfUnwind::PerfUnwind(QIODevice *output, const QString &systemRoot, const QString &debugPath,
                       const QString &extraLibsPath, const QString &appPath) :
    m_output(output), m_architecture(PerfRegisterInfo::ARCH_INVALID), m_systemRoot(systemRoot),
    m_extraLibsPath(extraLibsPath), m_appPath(appPath), m_sampleBufferSize(0)
{
    m_currentUnwind.unwind = this;
    m_offlineCallbacks.find_elf = dwfl_build_id_find_elf;
    m_offlineCallbacks.find_debuginfo =  dwfl_standard_find_debuginfo;
    m_offlineCallbacks.section_address = dwfl_offline_section_address;
    const QChar colon = QLatin1Char(':');
    QByteArray newDebugInfo = (colon + debugPath + colon + appPath + colon + extraLibsPath + colon
                               + systemRoot).toUtf8();
    m_debugInfoPath = new char[newDebugInfo.length() + 1];
    m_debugInfoPath[newDebugInfo.length()] = 0;
    std::memcpy(m_debugInfoPath, newDebugInfo.data(), newDebugInfo.length());
    m_offlineCallbacks.debuginfo_path = &m_debugInfoPath;
}

PerfUnwind::~PerfUnwind()
{
    foreach (const PerfRecordSample &sample, m_sampleBuffer)
        analyze(sample);

    delete[] m_debugInfoPath;
    qDeleteAll(m_symbolTables);
}

PerfSymbolTable *PerfUnwind::symbolTable(quint32 pid)
{
    PerfSymbolTable *&symbolTable = m_symbolTables[pid];
    if (!symbolTable)
        symbolTable = new PerfSymbolTable(pid, &m_offlineCallbacks, this);
    return symbolTable;
}

Dwfl *PerfUnwind::dwfl(quint32 pid)
{
    return symbolTable(pid)->attachDwfl(pid, &m_currentUnwind);
}

void PerfUnwind::registerElf(const PerfRecordMmap &mmap)
{
    symbolTable(mmap.pid())->registerElf(mmap, m_appPath, m_systemRoot, m_extraLibsPath);
}

void sendBuffer(QIODevice *output, const QByteArray &buffer)
{
    quint32 size = buffer.length();
    output->write(reinterpret_cast<char *>(&size), sizeof(quint32));
    output->write(buffer);
}

void PerfUnwind::comm(PerfRecordComm &comm)
{

    m_threads[comm.tid()] = QString::fromLocal8Bit(comm.comm());
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(Command)
                                               << comm.pid() << comm.tid() << m_threads[comm.tid()]
                                               << comm.time() << QVector<PerfUnwind::Frame>();
    sendBuffer(m_output, buffer);
}

Dwfl_Module *PerfUnwind::reportElf(quint64 ip, quint32 pid)
{
    auto symbols = symbolTable(pid);
    return symbols->reportElf(symbols->findElf(ip));
}

bool PerfUnwind::ipIsInKernelSpace(quint64 ip) const
{
    auto symbolTableIt = m_symbolTables.constFind(quint32(s_kernelPid));
    if (symbolTableIt == m_symbolTables.constEnd())
        return false;

    return symbolTableIt.value()->containsAddress(ip);
}

QDataStream &operator<<(QDataStream &stream, const PerfUnwind::Frame &frame)
{
    return stream << frame.locationId << frame.isKernel << frame.symbol << frame.elfFile;
}

QDataStream &operator<<(QDataStream &stream, const PerfUnwind::Location &location)
{
    return stream << location.address << location.file << location.line << location.column
                  << location.parentLocationId;
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
    ui->frames.append(ui->unwind->symbolTable(ui->sample->pid())->lookupFrame(pc_adjusted));
    return DWARF_CB_OK;
}

void PerfUnwind::unwindStack(Dwfl *dwfl)
{
    dwfl_getthread_frames(dwfl, m_currentUnwind.sample->pid(), frameCallback, &m_currentUnwind);
    if (m_currentUnwind.isInterworking()) {
        // If it's an ARM interworking veneer, we assume that we can find a return address in LR and
        // no stack has been used for the veneer itself.
        // The reasoning is that any symbol jumped to by the veneer has to work with or without
        // using the veneer. It needs a valid return address and when it returns the stack pointer
        // must be the same in both cases. Thus, the veneer cannot touch the stack pointer and there
        // has to be a return address in LR, provided by the caller.
        // So, just try again, and make setInitialRegisters use LR for IP.
        dwfl_getthread_frames(dwfl, m_currentUnwind.sample->pid(), frameCallback, &m_currentUnwind);
    }
}

void PerfUnwind::resolveCallchain()
{
    bool isKernel = false;
    PerfSymbolTable *symbols = symbolTable(m_currentUnwind.sample->pid());
    for (int i = 0; i < m_currentUnwind.sample->callchain().length(); ++i) {
        quint64 ip = m_currentUnwind.sample->callchain()[i];
        if (ip > PERF_CONTEXT_MAX) {
            switch (ip) {
            case PERF_CONTEXT_HV: // hypervisor
            case PERF_CONTEXT_KERNEL:
                if (!isKernel) {
                    symbols = symbolTable(s_kernelPid);
                    isKernel = true;
                }
                break;
            case PERF_CONTEXT_USER:
                if (isKernel) {
                    symbols = symbolTable(m_currentUnwind.sample->pid());
                    isKernel = false;
                }
                break;
            default:
                qWarning() << "invalid callchain context" << ip;
                return;
            }
        }

        // sometimes it skips the first user frame.
        if (i == 0 && !isKernel && ip != m_currentUnwind.sample->ip())
            m_currentUnwind.frames.append(symbols->lookupFrame(m_currentUnwind.sample->ip()));

        if (ip <= PERF_CONTEXT_MAX)
            m_currentUnwind.frames.append(symbols->lookupFrame(ip, isKernel));
    }
}

void PerfUnwind::sample(const PerfRecordSample &sample)
{
    m_sampleBuffer.append(sample);
    m_sampleBufferSize += sample.size();

    while (m_sampleBufferSize > s_maxSampleBufferSize) {
        const PerfRecordSample &sample = m_sampleBuffer.front();
        m_sampleBufferSize -= sample.size();
        analyze(sample);
        m_sampleBuffer.removeFirst();
    }
}

void PerfUnwind::analyze(const PerfRecordSample &sample)
{
    m_currentUnwind.broken = false;
    m_currentUnwind.sample = &sample;
    m_currentUnwind.frames.clear();

    const bool isKernel = ipIsInKernelSpace(sample.ip());

    PerfSymbolTable *userSymbols = symbolTable(sample.pid());
    userSymbols->updatePerfMap();

    if (sample.callchain().length() > 0)
        resolveCallchain();

    if (sample.registerAbi() != 0 && sample.userStack().length() > 0) {
        if (Dwfl *userDwfl = userSymbols->attachDwfl(sample.pid(), &m_currentUnwind))
            unwindStack(userDwfl);
    }

    // If nothing was found, at least look up the IP
    if (m_currentUnwind.frames.isEmpty()) {
        PerfSymbolTable *symbols = isKernel ? symbolTable(s_kernelPid) : userSymbols;
        m_currentUnwind.frames.append(symbols->lookupFrame(sample.ip(), isKernel));
    }

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly)
            << static_cast<quint8>(m_currentUnwind.broken ? BadStack : GoodStack) << sample.pid()
            << sample.tid() << m_threads[sample.tid()] << sample.time() << m_currentUnwind.frames;
    sendBuffer(m_output, buffer);
}

void PerfUnwind::fork(const PerfRecordFork &sample)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(ThreadStart)
                                               << sample.childPid() << sample.childTid()
                                               << m_threads[sample.childTid()] << sample.time()
                                               << QVector<PerfUnwind::Frame>();
    sendBuffer(m_output, buffer);
}

void PerfUnwind::exit(const PerfRecordExit &sample)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(ThreadEnd)
                                               << sample.childPid() << sample.childTid()
                                               << m_threads[sample.childTid()] << sample.time()
                                               << QVector<PerfUnwind::Frame>();
    sendBuffer(m_output, buffer);
}

void PerfUnwind::sendLocation(int id, const PerfUnwind::Location &location)
{
    QByteArray buffer;
    const PerfRecordSample *sample = m_currentUnwind.sample;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(LocationDefinition)
                                               << sample->pid() << sample->tid()
                                               << m_threads[sample->tid()] << sample->time()
                                               << id << location;
    sendBuffer(m_output, buffer);
}

int PerfUnwind::resolveLocation(const Location &location)
{
    auto symbolLocationIt = m_locations.find(location);
    if (symbolLocationIt == m_locations.end()) {
        symbolLocationIt = m_locations.insert(location, m_locations.size());
        sendLocation(symbolLocationIt.value(), location);
    }
    return symbolLocationIt.value();
}
