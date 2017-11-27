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
#include <QVersionNumber>
#include <QDir>

#include <cstring>

const qint32 PerfUnwind::s_kernelPid = -1;

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

void PerfUnwind::Stats::addEventTime(quint64 time)
{
    if (time && time < maxTime)
        maxReorderTime = std::max(maxReorderTime, maxTime - time);
    else
        maxTime = time;
}

void PerfUnwind::Stats::finishedRound()
{
    numSamples += numSamplesInRound;
    numMmaps += numMmapsInRound;

    maxSamplesPerRound = std::max(maxSamplesPerRound, numSamplesInRound);
    maxMmapsPerRound = std::max(maxMmapsPerRound, numMmapsInRound);
    maxContextSwitchesPerRound = std::max(maxContextSwitchesPerRound, numContextSwitchesInRound);
    numSamplesInRound = 0;
    numMmapsInRound = 0;
    numContextSwitchesInRound = 0;
    ++numRounds;

    maxTotalEventSizePerRound = std::max(maxTotalEventSizePerRound,
                                         totalEventSizePerRound);
    totalEventSizePerRound = 0;

    if (lastRoundTime > 0)
        maxTimeBetweenRounds = std::max(maxTimeBetweenRounds, maxTime - lastRoundTime);

    lastRoundTime = maxTime;
}

static int find_debuginfo(Dwfl_Module *module, void **userData, const char *moduleName,
                          Dwarf_Addr base, const char *file, const char *debugLink,
                          GElf_Word crc, char **debugInfoFilename)
{
    // data should have been set from PerfSymbolTable::reportElf
    Q_ASSERT(*userData);
    auto* symbolTable = reinterpret_cast<PerfSymbolTable*>(*userData);
    return symbolTable->findDebugInfo(module, moduleName, base, file, debugLink, crc, debugInfoFilename);
}

QString PerfUnwind::defaultDebugInfoPath()
{
    return QString::fromLatin1("%1usr%1lib%1debug%2%3%1.debug%2.debug")
            .arg(QDir::separator(), QDir::listSeparator(), QDir::homePath());
}

QString PerfUnwind::defaultKallsymsPath()
{
    return QString::fromLatin1("%1proc%1kallsyms").arg(QDir::separator());
}

PerfUnwind::PerfUnwind(QIODevice *output, const QString &systemRoot, const QString &debugPath,
                       const QString &extraLibsPath, const QString &appPath, bool printStats) :
    m_output(output), m_architecture(PerfRegisterInfo::ARCH_INVALID), m_systemRoot(systemRoot),
    m_extraLibsPath(extraLibsPath), m_appPath(appPath), m_debugPath(debugPath),
    m_kallsymsPath(QDir::rootPath() + defaultKallsymsPath()), m_ignoreKallsymsBuildId(false),
    m_maxEventBufferSize(10 * (1 << 20)), m_eventBufferSize(0), m_lastFlushMaxTime(0)
{
    m_stats.enabled = printStats;
    m_currentUnwind.unwind = this;
    m_offlineCallbacks.find_elf = dwfl_build_id_find_elf;
    m_offlineCallbacks.find_debuginfo = find_debuginfo;
    m_offlineCallbacks.section_address = dwfl_offline_section_address;
    const QChar separator = QDir::listSeparator();
    QByteArray newDebugInfo = (separator + debugPath + separator + appPath + separator
                               + extraLibsPath + separator + systemRoot).toUtf8();
    Q_ASSERT(newDebugInfo.length() >= 0);
    const uint debugInfoLength = static_cast<uint>(newDebugInfo.length());
    m_debugInfoPath = new char[debugInfoLength + 1];
    m_debugInfoPath[debugInfoLength] = 0;
    std::memcpy(m_debugInfoPath, newDebugInfo.data(), debugInfoLength);
    m_offlineCallbacks.debuginfo_path = &m_debugInfoPath;

    if (!printStats) {
        // Write minimal header, consisting of magic and data stream version we're going to use.
        const char magic[] = "QPERFSTREAM";
        output->write(magic, sizeof(magic));
        qint32 dataStreamVersion = qToLittleEndian(QDataStream::Qt_DefaultCompiledVersion);
        output->write(reinterpret_cast<const char *>(&dataStreamVersion), sizeof(qint32));
    }
}

PerfUnwind::~PerfUnwind()
{
    finishedRound();
    flushEventBuffer(0);

    delete[] m_debugInfoPath;
    qDeleteAll(m_symbolTables);

    if (m_stats.enabled) {
        QTextStream out(m_output);
        out << "samples: " << m_stats.numSamples << "\n";
        out << "mmaps: " << m_stats.numMmaps << "\n";
        out << "rounds: " << m_stats.numRounds << "\n";
        out << "buffer flushes: " << m_stats.numBufferFlushes << "\n";
        out << "samples time violations: " << m_stats.numTimeViolatingSamples << "\n";
        out << "mmaps time violations: " << m_stats.numTimeViolatingMmaps << "\n";
        out << "max samples per round: " << m_stats.maxSamplesPerRound << "\n";
        out << "max mmaps per round: " << m_stats.maxMmapsPerRound << "\n";
        out << "max context switches per round: " << m_stats.maxContextSwitchesPerRound << "\n";
        out << "max samples per flush: " << m_stats.maxSamplesPerFlush << "\n";
        out << "max mmaps per flush: " << m_stats.maxMmapsPerFlush << "\n";
        out << "max context switches per flush: " << m_stats.maxContextSwitchesPerFlush << "\n";
        out << "max buffer size: " << m_stats.maxBufferSize << "\n";
        out << "max total event size per round: " << m_stats.maxTotalEventSizePerRound << "\n";
        out << "max time: " << m_stats.maxTime << "\n";
        out << "max time between rounds: " << m_stats.maxTimeBetweenRounds << "\n";
        out << "max reorder time: " << m_stats.maxReorderTime << "\n";
    }
}

PerfSymbolTable *PerfUnwind::symbolTable(qint32 pid)
{
    PerfSymbolTable *&symbolTable = m_symbolTables[pid];
    if (!symbolTable)
        symbolTable = new PerfSymbolTable(pid, &m_offlineCallbacks, this);
    return symbolTable;
}

Dwfl *PerfUnwind::dwfl(qint32 pid)
{
    return symbolTable(pid)->attachDwfl(&m_currentUnwind);
}

void PerfUnwind::registerElf(const PerfRecordMmap &mmap)
{
    bufferEvent(mmap, &m_mmapBuffer, &m_stats.numMmapsInRound);
}

void PerfUnwind::sendBuffer(const QByteArray &buffer)
{
    if (m_stats.enabled)
        return;

    qint32 size = qToLittleEndian(buffer.length());
    m_output->write(reinterpret_cast<char *>(&size), sizeof(quint32));
    m_output->write(buffer);
}

void PerfUnwind::comm(const PerfRecordComm &comm)
{
    const qint32 commId = resolveString(comm.comm());
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(Command)
                                               << comm.pid() << comm.tid()  << comm.time()
                                               << commId;
    sendBuffer(buffer);
}

void PerfUnwind::attr(const PerfRecordAttr &attr)
{
    addAttributes(attr.attr(), attr.attr().name(), attr.ids());
}

void PerfUnwind::addAttributes(const PerfEventAttributes &attributes, const QByteArray &name,
                              const QList<quint64> &ids)
{
    const qint32 internalId = m_attributes.size();
    m_attributes.append(attributes);
    sendAttributes(internalId, attributes, name);

    if (ids.isEmpty()) {
        // If we only get one attribute, it doesn't have an ID.
        // The default ID for samples is 0, so we assign that here,
        // in order to look it up in analyze().
        m_attributeIds[0] = internalId;
    } else {
        foreach (quint64 id, ids)
            m_attributeIds[id] = internalId;
    }
}

void PerfUnwind::sendAttributes(qint32 id, const PerfEventAttributes &attributes, const QByteArray &name)
{
    const qint32 attrNameId = resolveString(name);

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(AttributesDefinition)
                                               << id << attributes.type()
                                               << attributes.config() << attrNameId
                                               << attributes.usesFrequency() << attributes.frequenyOrPeriod();
    sendBuffer(buffer);
}

void PerfUnwind::lost(const PerfRecordLost &lost)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(LostDefinition)
                                               << lost.pid() << lost.tid() << lost.time();
    sendBuffer(buffer);
}

void PerfUnwind::features(const PerfFeatures &features)
{
    const auto &eventDescs = features.eventDesc().eventDescs;
    for (const auto &desc : eventDescs)
        addAttributes(desc.attrs, desc.name, desc.ids);

    const auto perfVersion = QVersionNumber::fromString(QString::fromLatin1(features.version()));
    if (perfVersion >= QVersionNumber(3, 17))
        m_maxEventBufferSize = 0;

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(FeaturesDefinition)
                                               << features.hostName()
                                               << features.osRelease()
                                               << features.version()
                                               << features.architecture()
                                               << features.nrCpus()
                                               << features.cpuDesc()
                                               << features.cpuId()
                                               << features.totalMem()
                                               << features.cmdline()
                                               << features.buildId()
                                               << features.cpuTopology()
                                               << features.numaTopology()
                                               << features.branchStack()
                                               << features.pmuMappings()
                                               << features.groupDesc();
    sendBuffer(buffer);

    const auto &buildIds = features.buildId().buildIds;
    m_buildIds.reserve(buildIds.size());
    for (const auto &buildId : buildIds) {
        m_buildIds[buildId.fileName] = buildId.id;
    }
}

bool PerfUnwind::ipIsInKernelSpace(quint64 ip) const
{
    auto symbolTableIt = m_symbolTables.constFind(s_kernelPid);
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

    // do not query for activation directly, as this could potentially advance
    // the unwinder internally - we must first ensure the module for the pc
    // is reported
    if (!dwfl_frame_pc(state, &pc, NULL)
            || (ui->maxFrames != -1 && ui->frames.length() > ui->maxFrames)
            || pc == 0) {
        ui->firstGuessedFrame = ui->frames.length();
        qWarning() << dwfl_errmsg(dwfl_errno()) << ui->firstGuessedFrame;
        return DWARF_CB_ABORT;
    }

    auto* symbolTable = ui->unwind->symbolTable(ui->sample->pid());

    // ensure the module is reported
    // if that fails, we will still try to unwind based on frame pointer
    symbolTable->module(pc);

    // now we can query for the activation flag
    bool isactivation = false;
    dwfl_frame_pc(state, &pc, &isactivation);
    Dwarf_Addr pc_adjusted = pc - (isactivation ? 0 : 1);

    // isKernel = false as unwinding generally only works on user code
    bool isInterworking = false;
    const auto frame = symbolTable->lookupFrame(pc_adjusted, false, &isInterworking);
    if (symbolTable->cacheIsDirty())
        return DWARF_CB_ABORT;

    ui->frames.append(frame);
    if (isInterworking && ui->frames.length() == 1)
        ui->isInterworking = true;
    return DWARF_CB_OK;
}

void PerfUnwind::unwindStack(Dwfl *dwfl)
{
    dwfl_getthread_frames(dwfl, m_currentUnwind.sample->pid(), frameCallback, &m_currentUnwind);
    if (m_currentUnwind.isInterworking) {
        QVector<qint32> savedFrames = m_currentUnwind.frames;

        // If it's an ARM interworking veneer, we assume that we can find a return address in LR and
        // no stack has been used for the veneer itself.
        // The reasoning is that any symbol jumped to by the veneer has to work with or without
        // using the veneer. It needs a valid return address and when it returns the stack pointer
        // must be the same in both cases. Thus, the veneer cannot touch the stack pointer and there
        // has to be a return address in LR, provided by the caller.
        // So, just try again, and make setInitialRegisters use LR for IP.
        m_currentUnwind.frames.resize(1); // Keep the actual veneer frame
        dwfl_getthread_frames(dwfl, m_currentUnwind.sample->pid(), frameCallback, &m_currentUnwind);

        // If the LR trick didn't result in a longer stack trace than the regular unwinding, just
        // revert it.
        if (savedFrames.length() > m_currentUnwind.frames.length())
            m_currentUnwind.frames.swap(savedFrames);
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
                                              m_currentUnwind.sample->ip(), false,
                                              &m_currentUnwind.isInterworking));
        }

        if (ip <= PERF_CONTEXT_MAX) {
            m_currentUnwind.frames.append(symbols->lookupFrame(
                                              ip, isKernel,
                                              &m_currentUnwind.isInterworking));
        }

        if (symbols->cacheIsDirty())
            break;
    }
}

void PerfUnwind::sample(const PerfRecordSample &sample)
{
    bufferEvent(sample, &m_sampleBuffer, &m_stats.numSamplesInRound);
}

void PerfUnwind::analyze(const PerfRecordSample &sample)
{
    if (m_stats.enabled) // don't do any time intensive work in stats mode
        return;

    const bool isKernel = ipIsInKernelSpace(sample.ip());

    PerfSymbolTable *userSymbols = symbolTable(sample.pid());

    for (int unwindingAttempt = 0; unwindingAttempt < 2; ++unwindingAttempt) {
        m_currentUnwind.isInterworking = false;
        m_currentUnwind.firstGuessedFrame = -1;
        m_currentUnwind.sample = &sample;
        m_currentUnwind.frames.clear();

        userSymbols->updatePerfMap();

        Dwfl *userDwfl = userSymbols->attachDwfl(&m_currentUnwind);
        if (sample.callchain().length() > 0)
            resolveCallchain();

        // only try to unwind when resolveCallchain did not dirty the cache
        if (!userSymbols->cacheIsDirty()) {
            if (userDwfl && sample.registerAbi() != 0 && sample.userStack().length() > 0)
                unwindStack(userDwfl);
            else
                break;
        }

        // when the cache is dirty, we clean it up and try again, otherwise we can
        // stop as unwinding should have succeeded
        if (userSymbols->cacheIsDirty())
            userSymbols->clearCache(); // fail, try again
        else
            break; // success
    }

    // If nothing was found, at least look up the IP
    if (m_currentUnwind.frames.isEmpty()) {
        PerfSymbolTable *symbols = isKernel ? symbolTable(s_kernelPid) : userSymbols;
        m_currentUnwind.frames.append(symbols->lookupFrame(sample.ip(), isKernel,
                                                           &m_currentUnwind.isInterworking));
    }


    quint8 numGuessedFrames = 0;
    if (m_currentUnwind.firstGuessedFrame != -1) {
        // Squeeze it into 8 bits.
        int numGuessed = m_currentUnwind.frames.length() - m_currentUnwind.firstGuessedFrame;
        Q_ASSERT(numGuessed >= 0);
        numGuessedFrames
                = static_cast<quint8>(qMin(static_cast<int>(std::numeric_limits<quint8>::max()),
                                           numGuessed));
    }

    QVector<QPair<qint32, quint64>> values;
    if (sample.readFormats().isEmpty()) {
        values.push_back({m_attributeIds.value(sample.id(), -1), sample.period()});
    } else {
        for (const auto& f : sample.readFormats()) {
            values.push_back({m_attributeIds.value(f.id, -1), f.value});
        }
    }

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly)
            << static_cast<quint8>(Sample) << sample.pid()
            << sample.tid() << sample.time() << m_currentUnwind.frames
            << numGuessedFrames << values;
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

void PerfUnwind::sendString(qint32 id, const QByteArray& string)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(StringDefinition)
                                               << id << string;
    sendBuffer(buffer);
}

void PerfUnwind::sendLocation(qint32 id, const PerfUnwind::Location &location)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(LocationDefinition)
                                               << id << location;
    sendBuffer(buffer);
}

void PerfUnwind::sendSymbol(qint32 id, const PerfUnwind::Symbol &symbol)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(SymbolDefinition)
                                               << id << symbol;
    sendBuffer(buffer);
}

void PerfUnwind::sendError(ErrorCode error, const QString &message)
{
    qWarning().noquote().nospace() << error << ": " << message;
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(Error)
                                               << static_cast<qint32>(error) << message;
    sendBuffer(buffer);
}

void PerfUnwind::sendMissingElfFileError(const QByteArray &elfFile)
{
    if (m_missingElfFiles.contains(elfFile))
        return;

    m_missingElfFiles.insert(elfFile);

    if (elfFile.isEmpty()) {
        sendError(MissingElfFile,
                  tr("Failed to find ELF file for an instruction pointer address. "
                      "This can break stack unwinding and lead to missing symbols."));
    } else {
        sendError(MissingElfFile,
                  tr("Could not find ELF file for %1. This can break stack unwinding "
                     "and lead to missing symbols.").arg(QString::fromUtf8(elfFile)));
    }
}

void PerfUnwind::sendProgress(float percent)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(Progress)
                                               << percent;
    sendBuffer(buffer);
}

qint32 PerfUnwind::resolveString(const QByteArray& string)
{
    if (string.isEmpty())
        return -1;
    auto stringIt = m_strings.find(string);
    if (stringIt == m_strings.end()) {
        stringIt = m_strings.insert(string, m_strings.size());
        sendString(stringIt.value(), string);
    }
    return stringIt.value();
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

PerfKallsymEntry PerfUnwind::findKallsymEntry(quint64 address)
{
    if (m_kallsyms.isEmpty() && m_kallsyms.errorString().isEmpty()) {
        auto path = m_kallsymsPath;
        if (!m_ignoreKallsymsBuildId) {
            const auto &buildId = m_buildIds.value(QByteArrayLiteral("[kernel.kallsyms]"));
            if (!buildId.isEmpty()) {
                const auto debugPaths = m_debugPath.split(QDir::listSeparator(),
                                                        QString::SkipEmptyParts);
                for (const auto &debugPath : debugPaths) {
                    const QString buildIdPath = debugPath + QDir::separator() +
                                                QLatin1String("[kernel.kallsyms]") +
                                                QDir::separator() +
                                                QString::fromUtf8(buildId.toHex()) +
                                                QDir::separator() + QLatin1String("kallsyms");
                    if (QFile::exists(buildIdPath)) {
                        path = buildIdPath;
                        break;
                    }
                }
            }
        }
        if (!m_kallsyms.parseMapping(path)) {
            sendError(InvalidKallsyms,
                      tr("Failed to parse kernel symbol mapping file \"%1\": %2")
                            .arg(path, m_kallsyms.errorString()));
        }
    }
    return m_kallsyms.findEntry(address);
}

void PerfUnwind::finishedRound()
{
    if (m_stats.enabled)
        m_stats.finishedRound();

    // when we parse a perf data stream we may not know whether it contains
    // FINISHED_ROUND events. now we know, and thus we set the m_maxEventBufferSize
    // to 0 to disable the heuristic there. Instead, we will now rely on the finished
    // round events to tell us when to flush the event buffer
    if (!m_maxEventBufferSize) {
        // we only flush half of the events we got in this round
        // this work-arounds bugs in upstream perf which leads to time order violations
        // across FINISHED_ROUND events which should in theory never happen
        flushEventBuffer(m_eventBufferSize / 2);
    } else {
        m_maxEventBufferSize = 0;
    }
}

template<typename Event>
void PerfUnwind::bufferEvent(const Event &event, QList<Event> *buffer, uint *eventCounter)
{
    buffer->append(event);
    m_eventBufferSize += event.size();

    if (m_stats.enabled) {
        *eventCounter += 1;
        m_stats.maxBufferSize = std::max(m_eventBufferSize, m_stats.maxBufferSize);
        m_stats.totalEventSizePerRound += event.size();
        m_stats.addEventTime(event.time());
        // don't return early, stats should include our buffer behavior
    }

    if (m_maxEventBufferSize && m_eventBufferSize > m_maxEventBufferSize)
        flushEventBuffer(m_maxEventBufferSize / 2);
}

void PerfUnwind::flushEventBuffer(uint desiredBufferSize)
{
    auto sortByTime = [](const PerfRecord &lhs, const PerfRecord &rhs) {
        return lhs.time() < rhs.time();
    };
    std::sort(m_mmapBuffer.begin(), m_mmapBuffer.end(), sortByTime);
    std::sort(m_sampleBuffer.begin(), m_sampleBuffer.end(), sortByTime);
    std::sort(m_contextSwitchBuffer.begin(), m_contextSwitchBuffer.end(), sortByTime);

    if (m_stats.enabled) {
        for (const auto &sample : m_sampleBuffer) {
            if (sample.time() < m_lastFlushMaxTime)
                ++m_stats.numTimeViolatingSamples;
            else
                break;
        }
        for (const auto &mmap : m_mmapBuffer) {
            if (mmap.time() < m_lastFlushMaxTime)
                ++m_stats.numTimeViolatingMmaps;
            else
                break;
        }
    }

    if (!m_mmapBuffer.isEmpty() && m_mmapBuffer.first().time() < m_lastFlushMaxTime) {
        // when an mmap event is not following our desired time order, it can
        // severly break our analysis. as such we report a real error in these cases
        sendError(TimeOrderViolation,
                  tr("Time order violation of MMAP event across buffer flush detected. "
                     "Event time is %1, max time during last buffer flush was %2. "
                     "This potentially breaks the data analysis.")
                    .arg(m_mmapBuffer.first().time()).arg(m_lastFlushMaxTime));
    }

    auto mmapIt = m_mmapBuffer.begin();
    auto mmapEnd = m_mmapBuffer.end();

    auto sampleIt = m_sampleBuffer.begin();
    auto sampleEnd = m_sampleBuffer.end();

    auto contextSwitchIt = m_contextSwitchBuffer.begin();
    auto contextSwitchEnd = m_contextSwitchBuffer.end();

    for (; m_eventBufferSize > desiredBufferSize && sampleIt != sampleEnd; ++sampleIt) {
        const auto &sample = *sampleIt;

        if (sample.time() < m_lastFlushMaxTime) {
            qWarning() << "Time order violation across buffer flush detected:"
                       << "Event time =" << sample.time() << ","
                       << "max time during last buffer flush = " << m_lastFlushMaxTime;
            // we don't send an error for samples with broken times, since these
            // are usually harmless and actually occur relatively often
            // if desired, one can detect these issues on the client side anyways,
            // based on the sample times
        } else {
            m_lastFlushMaxTime = sample.time();
        }

        for (; mmapIt != mmapEnd && mmapIt->time() <= sample.time(); ++mmapIt) {
            if (!m_stats.enabled) {
                const auto &buildId = m_buildIds.value(mmapIt->filename());
                symbolTable(mmapIt->pid())->registerElf(*mmapIt, buildId);
            }
            m_eventBufferSize -= mmapIt->size();
        }

        for (; contextSwitchIt != contextSwitchEnd && contextSwitchIt->time() <= sample.time(); ++contextSwitchIt) {
            if (!m_stats.enabled) {
                sendContextSwitch(*contextSwitchIt);
            }
            m_eventBufferSize -= contextSwitchIt->size();
        }

        analyze(sample);
        m_eventBufferSize -= sample.size();
    }

    if (m_stats.enabled) {
        ++m_stats.numBufferFlushes;
        const auto samples = std::distance(m_sampleBuffer.begin(), sampleIt);
        Q_ASSERT(samples >= 0 && samples < std::numeric_limits<uint>::max());
        m_stats.maxSamplesPerFlush = std::max(static_cast<uint>(samples),
                                              m_stats.maxSamplesPerFlush);
        const auto mmaps = std::distance(m_mmapBuffer.begin(), mmapIt);
        Q_ASSERT(mmaps >= 0 && mmaps < std::numeric_limits<uint>::max());
        m_stats.maxMmapsPerFlush = std::max(static_cast<uint>(mmaps),
                                            m_stats.maxMmapsPerFlush);
        const auto contextSwitches = std::distance(m_contextSwitchBuffer.begin(), contextSwitchIt);
        Q_ASSERT(contextSwitches >= 0 && contextSwitches < std::numeric_limits<uint>::max());
        m_stats.maxContextSwitchesPerFlush = std::max(static_cast<uint>(contextSwitches),
                                                      m_stats.maxContextSwitchesPerFlush);
    }

    m_sampleBuffer.erase(m_sampleBuffer.begin(), sampleIt);
    m_mmapBuffer.erase(m_mmapBuffer.begin(), mmapIt);
    m_contextSwitchBuffer.erase(m_contextSwitchBuffer.begin(), contextSwitchIt);
}

void PerfUnwind::contextSwitch(const PerfRecordContextSwitch& contextSwitch)
{
    bufferEvent(contextSwitch, &m_contextSwitchBuffer, &m_stats.numContextSwitchesInRound);
}

void PerfUnwind::sendContextSwitch(const PerfRecordContextSwitch& contextSwitch)
{
    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(ContextSwitchDefinition)
                                               << contextSwitch.pid() << contextSwitch.tid()
                                               << contextSwitch.time()
                                               << bool(contextSwitch.misc() & PERF_RECORD_MISC_SWITCH_OUT);
    sendBuffer(buffer);
}
