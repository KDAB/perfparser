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
#include <QtEndian>

#include <cstring>

uint qHash(const PerfUnwind::Location &location, uint seed)
{
    QtPrivate::QHashCombine hash;
    seed = hash(seed, location.address);
    seed = hash(seed, location.file);
    seed = hash(seed, location.pid);
    seed = hash(seed, location.line);
    seed = hash(seed, location.column);
    return seed;
}

bool operator==(const PerfUnwind::Location &a, const PerfUnwind::Location &b)
{
    return a.address == b.address && a.file == b.file && a.pid == b.pid && a.line == b.line
            && a.column == b.column;
}

PerfUnwind::PerfUnwind(QIODevice *output, const QString &systemRoot, const QString &debugPath,
                       const QString &extraLibsPath, const QString &appPath) :
    m_output(output), m_architecture(PerfRegisterInfo::ARCH_INVALID), m_systemRoot(systemRoot),
    m_extraLibsPath(extraLibsPath), m_appPath(appPath), m_nextAttributeId(0), m_sampleBufferSize(0)
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

    // Write minimal header, consisting of magic and data stream version we're going to use.
    const char magic[] = "QPERFSTREAM";
    output->write(magic, sizeof(magic));
    qint32 dataStreamVersion = qToLittleEndian(QDataStream::Qt_DefaultCompiledVersion);
    output->write(reinterpret_cast<const char *>(&dataStreamVersion), sizeof(qint32));
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

Dwfl *PerfUnwind::dwfl(quint32 pid, quint64 timestamp)
{
    return symbolTable(pid)->attachDwfl(timestamp, &m_currentUnwind);
}

void PerfUnwind::registerElf(const PerfRecordMmap &mmap)
{
    symbolTable(mmap.pid())->registerElf(mmap, m_appPath, m_systemRoot, m_extraLibsPath);
}

void PerfUnwind::sendBuffer(const QByteArray &buffer)
{
    quint32 size = qToLittleEndian(buffer.length());
    m_output->write(reinterpret_cast<char *>(&size), sizeof(quint32));
    m_output->write(buffer);
}

void PerfUnwind::comm(const PerfRecordComm &comm)
{

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(Command)
                                               << comm.pid() << comm.tid()  << comm.time()
                                               << comm.comm();
    sendBuffer(buffer);
}

void PerfUnwind::attr(const PerfRecordAttr &attr)
{
    QByteArray buffer;

    foreach (quint64 id, attr.ids())
        m_attributeIds[id] = m_nextAttributeId;

    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(AttributesDefinition)
                                               << attr.pid() << attr.tid() << attr.time()
                                               << m_nextAttributeId << attr.attr().type()
                                               << attr.attr().config() << attr.attr().name();
    sendBuffer(buffer);
    ++m_nextAttributeId;
}

Dwfl_Module *PerfUnwind::reportElf(quint64 ip, quint32 pid, quint64 timestamp)
{
    auto symbols = symbolTable(pid);
    return symbols->reportElf(symbols->findElf(ip, timestamp));
}

bool PerfUnwind::ipIsInKernelSpace(quint64 ip) const
{
    auto symbolTableIt = m_symbolTables.constFind(quint32(s_kernelPid));
    if (symbolTableIt == m_symbolTables.constEnd())
        return false;

    return symbolTableIt.value()->containsAddress(ip);
}

QDataStream &operator<<(QDataStream &stream, const PerfUnwind::Location &location)
{
    return stream << location.address << location.file << location.pid << location.line
                  << location.column << location.parentLocationId;
}

QDataStream &operator<<(QDataStream &stream, const PerfUnwind::Symbol &symbol)
{
    return stream << symbol.name << symbol.binary << symbol.isKernel;
}

static int frameCallback(Dwfl_Frame *state, void *arg)
{
    Dwarf_Addr pc = 0;
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);

    bool isactivation;
    if (!dwfl_frame_pc(state, &pc, &isactivation)
            || ui->frames.length() > PerfUnwind::s_maxFrames
            || pc == 0) {
        ui->firstGuessedFrame = ui->frames.length();
        qWarning() << dwfl_errmsg(dwfl_errno()) << ui->firstGuessedFrame;
        return DWARF_CB_ABORT;
    }

    Dwarf_Addr pc_adjusted = pc - (isactivation ? 0 : 1);

    // isKernel = false as unwinding generally only works on user code
    ui->frames.append(ui->unwind->symbolTable(ui->sample->pid())->lookupFrame(
                          pc_adjusted, ui->sample->time(), false, &ui->isInterworking));
    return DWARF_CB_OK;
}

void PerfUnwind::unwindStack(Dwfl *dwfl)
{
    dwfl_getthread_frames(dwfl, m_currentUnwind.sample->pid(), frameCallback, &m_currentUnwind);
    if (m_currentUnwind.frames.length() == 1 && m_currentUnwind.isInterworking) {
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
        if (i == 0 && !isKernel && ip != m_currentUnwind.sample->ip()) {
            m_currentUnwind.frames.append(symbols->lookupFrame(
                                              m_currentUnwind.sample->ip(),
                                              m_currentUnwind.sample->time(), false,
                                              &m_currentUnwind.isInterworking));
        }

        if (ip <= PERF_CONTEXT_MAX) {
            m_currentUnwind.frames.append(symbols->lookupFrame(
                                              ip, m_currentUnwind.sample->time(), isKernel,
                                              &m_currentUnwind.isInterworking));
        }
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
    m_currentUnwind.isInterworking = false;
    m_currentUnwind.firstGuessedFrame = -1;
    m_currentUnwind.sample = &sample;
    m_currentUnwind.frames.clear();

    const bool isKernel = ipIsInKernelSpace(sample.ip());

    PerfSymbolTable *userSymbols = symbolTable(sample.pid());
    userSymbols->updatePerfMap();

    // Do this before any lookupFrame() calls; we want to clear the caches if timestamps reset.
    Dwfl *userDwfl = userSymbols->attachDwfl(sample.time(), &m_currentUnwind);
    if (sample.callchain().length() > 0)
        resolveCallchain();

    if (userDwfl && sample.registerAbi() != 0 && sample.userStack().length() > 0)
        unwindStack(userDwfl);

    // If nothing was found, at least look up the IP
    if (m_currentUnwind.frames.isEmpty()) {
        PerfSymbolTable *symbols = isKernel ? symbolTable(s_kernelPid) : userSymbols;
        m_currentUnwind.frames.append(symbols->lookupFrame(sample.ip(), sample.time(), isKernel,
                                                           &m_currentUnwind.isInterworking));
    }

    const quint8 numGuessedFrames = (m_currentUnwind.firstGuessedFrame == -1)
            ? 0 : m_currentUnwind.frames.length() - m_currentUnwind.firstGuessedFrame;
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly)
            << static_cast<quint8>(Sample) << sample.pid()
            << sample.tid() << sample.time() << m_currentUnwind.frames
            << numGuessedFrames << m_attributeIds[sample.id()];
    sendBuffer(buffer);
}

void PerfUnwind::fork(const PerfRecordFork &sample)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(ThreadStart)
                                               << sample.childPid() << sample.childTid()
                                               << sample.time();
    sendBuffer(buffer);
}

void PerfUnwind::exit(const PerfRecordExit &sample)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(ThreadEnd)
                                               << sample.childPid() << sample.childTid()
                                               << sample.time();
    sendBuffer(buffer);
}

void PerfUnwind::sendLocation(qint32 id, const PerfUnwind::Location &location)
{
    QByteArray buffer;
    const PerfRecordSample *sample = m_currentUnwind.sample;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(LocationDefinition)
                                               << sample->pid() << sample->tid()
                                               << sample->time() << id << location;
    sendBuffer(buffer);
}

void PerfUnwind::sendSymbol(qint32 id, const PerfUnwind::Symbol &symbol)
{
    QByteArray buffer;
    const PerfRecordSample *sample = m_currentUnwind.sample;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(SymbolDefinition)
                                               << sample->pid() << sample->tid()
                                               << sample->time() << id << symbol;
    sendBuffer(buffer);
}

int PerfUnwind::lookupLocation(const PerfUnwind::Location &location) const
{
    return m_locations.value(location, -1);
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

bool PerfUnwind::hasSymbol(int locationId) const
{
    return m_symbols.contains(locationId);
}

void PerfUnwind::resolveSymbol(int locationId, const PerfUnwind::Symbol &symbol)
{
    m_symbols.insert(locationId, symbol);
    sendSymbol(locationId, symbol);
}
