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

#include "perfregisterinfo.h"
#include "perfsymboltable.h"
#include "perfunwind.h"

#include <QDebug>
#include <QDir>
#include <QVersionNumber>
#include <QtEndian>

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
    maxTaskEventsPerRound = std::max(maxTaskEventsPerRound, numTaskEventsInRound);
    numSamplesInRound = 0;
    numMmapsInRound = 0;
    numTaskEventsInRound = 0;
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
    m_lastEventBufferSize(1 << 20), m_maxEventBufferSize(1 << 30), m_targetEventBufferSize(1 << 25),
    m_eventBufferSize(0), m_timeOrderViolations(0), m_lastFlushMaxTime(0)
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
    finalize();

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
        out << "max task events per round: " << m_stats.maxTaskEventsPerRound << "\n";
        out << "max samples per flush: " << m_stats.maxSamplesPerFlush << "\n";
        out << "max mmaps per flush: " << m_stats.maxMmapsPerFlush << "\n";
        out << "max task events per flush: " << m_stats.maxTaskEventsPerFlush << "\n";
        out << "max buffer size: " << m_stats.maxBufferSize << "\n";
        out << "max total event size per round: " << m_stats.maxTotalEventSizePerRound << "\n";
        out << "max time: " << m_stats.maxTime << "\n";
        out << "max time between rounds: " << m_stats.maxTimeBetweenRounds << "\n";
        out << "max reorder time: " << m_stats.maxReorderTime << "\n";
    }
}

void PerfUnwind::setMaxEventBufferSize(uint size)
{
    m_maxEventBufferSize = size;
    if (size < m_targetEventBufferSize)
        setTargetEventBufferSize(size);
}

void PerfUnwind::setTargetEventBufferSize(uint size)
{
    m_lastEventBufferSize = m_targetEventBufferSize;
    m_targetEventBufferSize = size;
    if (size > m_maxEventBufferSize)
        setMaxEventBufferSize(size);
}

void PerfUnwind::revertTargetEventBufferSize()
{
    setTargetEventBufferSize(m_lastEventBufferSize);
}

bool PerfUnwind::hasTracePointAttributes() const
{
    for (auto &attributes : m_attributes) {
        if (attributes.type() == PerfEventAttributes::TYPE_TRACEPOINT)
            return true;
    }
    return false;
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

    bufferEvent(TaskEvent{comm.pid(), comm.tid(), comm.time(), comm.cpu(),
                          commId, Command},
                &m_taskEventsBuffer, &m_stats.numTaskEventsInRound);
}

void PerfUnwind::attr(const PerfRecordAttr &attr)
{
    addAttributes(attr.attr(), attr.attr().name(), attr.ids());
}

void PerfUnwind::addAttributes(const PerfEventAttributes &attributes, const QByteArray &name,
                              const QList<quint64> &ids)
{
    auto filteredIds = ids;
    // If we only get one attribute, it doesn't have an ID.
    // The default ID for samples is 0, so we assign that here,
    // in order to look it up in analyze().
    filteredIds << 0;

    {
        // remove attributes that are known already
        auto it = std::remove_if(filteredIds.begin(), filteredIds.end(),
                                 [this] (quint64 id) {
                                     return m_attributeIds.contains(id);
                                });
        filteredIds.erase(it, filteredIds.end());
    }

    // Switch to dynamic buffering if it's a trace point
    if (attributes.type() == PerfEventAttributes::TYPE_TRACEPOINT && m_targetEventBufferSize == 0) {
        qDebug() << "Trace point attributes detected. Switching to dynamic buffering";
        revertTargetEventBufferSize();
    }

    if (filteredIds.isEmpty())
        return;

    const qint32 internalId = m_attributes.size();
    m_attributes.append(attributes);
    sendAttributes(internalId, attributes, name);

    foreach (quint64 id, filteredIds)
        m_attributeIds[id] = internalId;
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

void PerfUnwind::sendEventFormat(qint32 id, const EventFormat &format)
{
    const qint32 systemId = resolveString(format.system);
    const qint32 nameId = resolveString(format.name);

    for (const FormatField &field : format.commonFields)
        resolveString(field.name);

    for (const FormatField &field : format.fields)
        resolveString(field.name);

    QByteArray buffer;
    QDataStream(&buffer, QIODevice::WriteOnly) << static_cast<quint8>(TracePointFormat) << id
                                               << systemId << nameId << format.flags;
    sendBuffer(buffer);
}

void PerfUnwind::lost(const PerfRecordLost &lost)
{
    bufferEvent(TaskEvent{lost.pid(), lost.tid(), lost.time(), lost.cpu(),
                          0, LostDefinition},
                &m_taskEventsBuffer, &m_stats.numTaskEventsInRound);
}

void PerfUnwind::features(const PerfFeatures &features)
{
    tracing(features.tracingData());

    const auto &eventDescs = features.eventDesc().eventDescs;
    for (const auto &desc : eventDescs)
        addAttributes(desc.attrs, desc.name, desc.ids);

    const auto perfVersion = QVersionNumber::fromString(QString::fromLatin1(features.version()));
    if (perfVersion >= QVersionNumber(3, 17) && m_timeOrderViolations == 0) {
        if (!hasTracePointAttributes()) {
            qDebug() << "Linux version" << features.version()
                     << "detected. Switching to automatic buffering.";
            setTargetEventBufferSize(0);
        }
    }

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
                                               << features.buildIds()
                                               << features.cpuTopology()
                                               << features.numaTopology()
                                               << features.pmuMappings()
                                               << features.groupDescs();
    sendBuffer(buffer);

    const auto buildIds = features.buildIds();
    m_buildIds.reserve(buildIds.size());
    for (const auto &buildId : buildIds) {
        m_buildIds[buildId.fileName] = buildId.id;
    }
}

void PerfUnwind::tracing(const PerfTracingData &tracingData)
{
    m_tracingData = tracingData;
    const auto &formats = tracingData.eventFormats();
    for (auto it = formats.constBegin(), end = formats.constEnd(); it != end; ++it)
        sendEventFormat(it.key(), it.value());
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
    return stream << symbol.name << symbol.binary << symbol.path << symbol.isKernel;
}

static int frameCallback(Dwfl_Frame *state, void *arg)
{
    Dwarf_Addr pc = 0;
    PerfUnwind::UnwindInfo *ui = static_cast<PerfUnwind::UnwindInfo *>(arg);

    // do not query for activation directly, as this could potentially advance
    // the unwinder internally - we must first ensure the module for the pc
    // is reported
    if (!dwfl_frame_pc(state, &pc, nullptr)
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

void PerfUnwind::unwindStack()
{
    Dwfl *dwfl = symbolTable(m_currentUnwind.sample->pid())->attachDwfl(&m_currentUnwind);
    if (!dwfl)
        return;

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
    bool addedUserFrames = false;
    PerfSymbolTable *symbols = symbolTable(m_currentUnwind.sample->pid());

    auto reportIp = [&](quint64 ip) -> bool {
        symbols->attachDwfl(&m_currentUnwind);
        m_currentUnwind.frames.append(symbols->lookupFrame(ip, isKernel,
                                            &m_currentUnwind.isInterworking));
        return !symbols->cacheIsDirty();
    };

    // when we have a non-empty branch stack, we need to skip any non-kernel IPs
    // in the normal callchain. The branch stack contains the non-kernel IPs then.
    const bool hasBranchStack = !m_currentUnwind.sample->branchStack().isEmpty();

    for (int i = 0, c = m_currentUnwind.sample->callchain().size(); i < c; ++i) {
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
                qWarning() << "invalid callchain context" << hex << ip;
                return;
            }
        } else {
            // prefer user frames from branch stack if available
            if (hasBranchStack && !isKernel)
                break;

            // sometimes it skips the first user frame.
            if (!addedUserFrames && !isKernel && ip != m_currentUnwind.sample->ip()) {
                if (!reportIp(m_currentUnwind.sample->ip()))
                    return;
            }

            if (!reportIp(ip))
                return;

            if (!isKernel)
                addedUserFrames = true;
        }
    }

    // when we are still in the kernel, we cannot have a meaningful branch stack
    if (isKernel)
        return;

    // if available, also resolve the callchain stored in the branch stack:
    // caller is stored in "from", callee is stored in "to"
    // so the branch is made up of the first callee and all callers
    for (int i = 0, c = m_currentUnwind.sample->branchStack().size(); i < c; ++i) {
        const auto& entry = m_currentUnwind.sample->branchStack()[i];
        if (i == 0 && !reportIp(entry.to))
            return;
        if (!reportIp(entry.from))
            return;
    }
}

void PerfUnwind::sample(const PerfRecordSample &sample)
{
    bufferEvent(sample, &m_sampleBuffer, &m_stats.numSamplesInRound);
}

template<typename Number>
Number readFromArray(const QByteArray &data, quint32 offset, bool byteSwap)
{
    const Number number = *reinterpret_cast<const Number *>(data.data() + offset);
    return byteSwap ? qbswap(number) : number;
}

QVariant readTraceItem(const QByteArray &data, quint32 offset, quint32 size, bool isSigned,
                       bool byteSwap)
{
    if (isSigned) {
        switch (size) {
        case 1: return readFromArray<qint8>(data, offset, byteSwap);
        case 2: return readFromArray<qint16>(data, offset, byteSwap);
        case 4: return readFromArray<qint32>(data, offset, byteSwap);
        case 8: return readFromArray<qint64>(data, offset, byteSwap);
        default: return QVariant::Invalid;
        }
    } else {
        switch (size) {
        case 1: return readFromArray<quint8>(data, offset, byteSwap);
        case 2: return readFromArray<quint16>(data, offset, byteSwap);
        case 4: return readFromArray<quint32>(data, offset, byteSwap);
        case 8: return readFromArray<quint64>(data, offset, byteSwap);
        default: return QVariant::Invalid;
        }
    }
}

QVariant PerfUnwind::readTraceData(const QByteArray &data, const FormatField &field, bool byteSwap)
{
    // TODO: validate that it actually works like this.
    if (field.offset > quint32(std::numeric_limits<int>::max())
            || field.size > quint32(std::numeric_limits<int>::max())
            || field.offset + field.size > quint32(std::numeric_limits<int>::max())
            || static_cast<int>(field.offset + field.size) > data.length()) {
        return QVariant::Invalid;
    }

    if (field.flags & FIELD_IS_ARRAY) {
        if (field.flags & FIELD_IS_DYNAMIC) {
            const quint32 dynamicOffsetAndSize = readTraceItem(data, field.offset, field.size,
                                                               false, byteSwap).toUInt();
            FormatField newField = field;
            newField.offset = dynamicOffsetAndSize & 0xffff;
            newField.size = dynamicOffsetAndSize >> 16;
            newField.flags = field.flags & (~FIELD_IS_DYNAMIC);
            return readTraceData(data, newField, byteSwap);
        }
        if (field.flags & FIELD_IS_STRING) {
            return data.mid(static_cast<int>(field.offset), static_cast<int>(field.size));
        } else {
            QList<QVariant> result;
            for (quint32 i = 0; i < field.size; i += field.elementsize) {
                result.append(readTraceItem(data, field.offset + i, field.elementsize,
                                            field.flags & FIELD_IS_SIGNED, byteSwap));
            }
            return result;
        }
    } else {
        return readTraceItem(data, field.offset, field.size, field.flags & FIELD_IS_SIGNED,
                             byteSwap);
    }
}

void PerfUnwind::analyze(const PerfRecordSample &sample)
{
    if (m_stats.enabled) // don't do any time intensive work in stats mode
        return;

    PerfSymbolTable *kernelSymbols = symbolTable(s_kernelPid);
    PerfSymbolTable *userSymbols = symbolTable(sample.pid());

    for (int unwindingAttempt = 0; unwindingAttempt < 2; ++unwindingAttempt) {
        m_currentUnwind.isInterworking = false;
        m_currentUnwind.firstGuessedFrame = -1;
        m_currentUnwind.sample = &sample;
        m_currentUnwind.frames.clear();

        userSymbols->updatePerfMap();
        if (!sample.callchain().isEmpty() || !sample.branchStack().isEmpty())
            resolveCallchain();

        bool userDirty = userSymbols->cacheIsDirty();
        bool kernelDirty = kernelSymbols->cacheIsDirty();

        // only try to unwind when resolveCallchain did not dirty the cache
        if (!userDirty && !kernelDirty) {
            if (sample.registerAbi() != 0 && sample.userStack().length() > 0) {
                unwindStack();
                userDirty = userSymbols->cacheIsDirty();
            } else {
                break;
            }
        }

        // when the cache is dirty, we clean it up and try again, otherwise we can
        // stop as unwinding should have succeeded
        if (userDirty)
            userSymbols->clearCache(); // fail, try again
        if (kernelDirty)
            kernelSymbols->clearCache();
        if (!userDirty && !kernelDirty)
            break; // success
    }

    // If nothing was found, at least look up the IP
    if (m_currentUnwind.frames.isEmpty()) {
        const bool isKernel = ipIsInKernelSpace(sample.ip());
        PerfSymbolTable *ipSymbols = isKernel ? kernelSymbols : userSymbols;
        m_currentUnwind.frames.append(ipSymbols->lookupFrame(sample.ip(), isKernel,
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

    EventType type = Sample;
    qint32 eventFormatId = -1;
    const qint32 attributesId = m_attributeIds.value(sample.id(), -1);
    if (attributesId != -1) {
        const auto &attribute = m_attributes.at(attributesId);
        if (attribute.type() == PerfEventAttributes::TYPE_TRACEPOINT) {
            type = TracePointSample;
            if (attribute.config() > quint64(std::numeric_limits<qint32>::max()))
                qWarning() << "Excessively large event format ID" << attribute.config();
            else
                eventFormatId = static_cast<qint32>(attribute.config());
        }
    }

    QVector<QPair<qint32, quint64>> values;
    if (sample.readFormats().isEmpty()) {
        values.push_back({ attributesId, sample.period() });
    } else {
        for (const auto& f : sample.readFormats()) {
            values.push_back({ m_attributeIds.value(f.id, -1), f.value });
        }
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << static_cast<quint8>(type) << sample.pid()
           << sample.tid() << sample.time() << sample.cpu() << m_currentUnwind.frames
           << numGuessedFrames << values;

    if (type == TracePointSample) {
        QHash<qint32, QVariant> traceData;
        const QByteArray &data = sample.rawData();
        const EventFormat &format = m_tracingData.eventFormat(eventFormatId);
        for (const FormatField &field : format.fields) {
            traceData[lookupString(field.name)]
                    = readTraceData(data, field, m_byteOrder != QSysInfo::ByteOrder);
        }
        stream << traceData;
    }

    sendBuffer(buffer);
}

void PerfUnwind::fork(const PerfRecordFork &sample)
{
    bufferEvent(TaskEvent{sample.childPid(), sample.childTid(), sample.time(), sample.cpu(),
                          0, ThreadStart},
                &m_taskEventsBuffer, &m_stats.numTaskEventsInRound);
}

void PerfUnwind::exit(const PerfRecordExit &sample)
{
    bufferEvent(TaskEvent{sample.childPid(), sample.childTid(), sample.time(), sample.cpu(),
                          0, ThreadEnd},
                &m_taskEventsBuffer, &m_stats.numTaskEventsInRound);
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
    Q_ASSERT(location.pid);
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

qint32 PerfUnwind::lookupString(const QByteArray &string)
{
    return m_strings.value(string, -1);
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
    if (!m_targetEventBufferSize) {
        // we only flush half of the events we got in this round
        // this work-arounds bugs in upstream perf which leads to time order violations
        // across FINISHED_ROUND events which should in theory never happen
        flushEventBuffer(m_eventBufferSize / 2);
    } else if (m_timeOrderViolations == 0 && !hasTracePointAttributes()) {
        qDebug() << "FINISHED_ROUND detected. Switching to automatic buffering";
        setTargetEventBufferSize(0);
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

    if (m_targetEventBufferSize && m_eventBufferSize > m_targetEventBufferSize)
        flushEventBuffer(m_targetEventBufferSize / 2);
}

void PerfUnwind::forwardMmapBuffer(QList<PerfRecordMmap>::Iterator &mmapIt,
                                   const QList<PerfRecordMmap>::Iterator &mmapEnd,
                                   quint64 timestamp)
{
    for (; mmapIt != mmapEnd && mmapIt->time() <= timestamp; ++mmapIt) {
        if (!m_stats.enabled) {
            const auto &buildId = m_buildIds.value(mmapIt->filename());
            symbolTable(mmapIt->pid())->registerElf(*mmapIt, buildId);
        }
        m_eventBufferSize -= mmapIt->size();
    }
}

template<typename T>
bool sortByTime(const T& lhs, const T& rhs)
{
    return lhs.time() < rhs.time();
}

void PerfUnwind::flushEventBuffer(uint desiredBufferSize)
{
    // stable sort here to keep order of events with the same time
    // esp. when we runtime-attach, we will get lots of mmap events with time 0
    // which we must not shuffle
    std::stable_sort(m_mmapBuffer.begin(), m_mmapBuffer.end(), sortByTime<PerfRecord>);
    std::stable_sort(m_sampleBuffer.begin(), m_sampleBuffer.end(), sortByTime<PerfRecord>);
    std::stable_sort(m_taskEventsBuffer.begin(), m_taskEventsBuffer.end(), sortByTime<TaskEvent>);

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

    bool violatesTimeOrder = false;
    if (!m_mmapBuffer.isEmpty() && m_mmapBuffer.first().time() < m_lastFlushMaxTime) {
        // when an mmap event is not following our desired time order, it can
        // severly break our analysis. as such we report a real error in these cases
        sendError(TimeOrderViolation,
                  tr("Time order violation of MMAP event across buffer flush detected. "
                     "Event time is %1, max time during last buffer flush was %2. "
                     "This potentially breaks the data analysis.")
                    .arg(m_mmapBuffer.first().time()).arg(m_lastFlushMaxTime));
        violatesTimeOrder = true;
    }

    auto mmapIt = m_mmapBuffer.begin();
    auto mmapEnd = m_mmapBuffer.end();

    auto sampleIt = m_sampleBuffer.begin();
    auto sampleEnd = m_sampleBuffer.end();

    uint bufferSize = m_eventBufferSize;

    auto taskEventIt = m_taskEventsBuffer.begin();
    auto taskEventEnd = m_taskEventsBuffer.end();

    for (; m_eventBufferSize > desiredBufferSize && sampleIt != sampleEnd; ++sampleIt) {
        const quint64 timestamp = sampleIt->time();

        if (timestamp < m_lastFlushMaxTime) {
            if (!violatesTimeOrder) {
                qWarning() << "Time order violation across buffer flush detected:"
                           << "Event time =" << timestamp << ","
                           << "max time during last buffer flush = " << m_lastFlushMaxTime;
                // we don't send an error for samples with broken times, since these
                // are usually harmless and actually occur relatively often
                // if desired, one can detect these issues on the client side anyways,
                // based on the sample times
                violatesTimeOrder = true;
            }
        } else {
            // We've forwarded past the violating events as we couldn't do anything about those
            // anymore. Now break and wait for the larger buffer to fill up, so that we avoid
            // further violations in the yet to be processed events.
            if (violatesTimeOrder) {
                // Process any remaining mmap events violating the previous buffer flush.
                // Otherwise we would catch the same ones again in the next round.
                forwardMmapBuffer(mmapIt, mmapEnd, m_lastFlushMaxTime);
                break;
            }

            m_lastFlushMaxTime = timestamp;
        }

        forwardMmapBuffer(mmapIt, mmapEnd, timestamp);

        for (; taskEventIt != taskEventEnd && taskEventIt->time() <= sampleIt->time();
             ++taskEventIt) {
            if (!m_stats.enabled) {
                sendTaskEvent(*taskEventIt);
            }
            m_eventBufferSize -= taskEventIt->size();
        }

        analyze(*sampleIt);
        m_eventBufferSize -= sampleIt->size();
    }

    // also flush task events after samples got depleted
    // this ensures we send all of them, even for situations where the client
    // application is not CPU-heavy but rather sleeps most of the time
    for (; m_eventBufferSize > desiredBufferSize && taskEventIt != taskEventEnd; ++taskEventIt) {
        if (!m_stats.enabled) {
            sendTaskEvent(*taskEventIt);
        }
        m_eventBufferSize -= taskEventIt->size();
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
        const auto taskEvents = std::distance(m_taskEventsBuffer.begin(), taskEventIt);
        Q_ASSERT(taskEvents >= 0 && taskEvents < std::numeric_limits<uint>::max());
        m_stats.maxTaskEventsPerFlush = std::max(static_cast<uint>(taskEvents),
                                                      m_stats.maxTaskEventsPerFlush);
    }

    m_sampleBuffer.erase(m_sampleBuffer.begin(), sampleIt);
    m_mmapBuffer.erase(m_mmapBuffer.begin(), mmapIt);
    m_taskEventsBuffer.erase(m_taskEventsBuffer.begin(), taskEventIt);

    if (!violatesTimeOrder)
        return;

    // Increase buffer size to reduce future time order violations
    ++m_timeOrderViolations;

    // If we had a larger event buffer before, increase.
    if (bufferSize < m_lastEventBufferSize)
        bufferSize = m_lastEventBufferSize;

    // Double the size, clamping by UINT_MAX.
    if (bufferSize > std::numeric_limits<uint>::max() / 2)
        bufferSize = std::numeric_limits<uint>::max();
    else
        bufferSize *= 2;

    // Clamp by max buffer size.
    if (bufferSize > m_maxEventBufferSize)
        bufferSize = m_maxEventBufferSize;

    qDebug() << "Increasing buffer size to" << bufferSize;
    setTargetEventBufferSize(bufferSize);
}

void PerfUnwind::contextSwitch(const PerfRecordContextSwitch& contextSwitch)
{
    bufferEvent(TaskEvent{contextSwitch.pid(), contextSwitch.tid(),
                contextSwitch.time(), contextSwitch.cpu(),
                contextSwitch.misc() & PERF_RECORD_MISC_SWITCH_OUT, ContextSwitchDefinition},
                &m_taskEventsBuffer, &m_stats.numTaskEventsInRound);
}

void PerfUnwind::sendTaskEvent(const TaskEvent& taskEvent)
{
    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << static_cast<quint8>(taskEvent.m_type)
           << taskEvent.m_pid << taskEvent.m_tid
           << taskEvent.m_time << taskEvent.m_cpu;

    if (taskEvent.m_type == ContextSwitchDefinition)
        stream << static_cast<bool>(taskEvent.m_payload);
    else if (taskEvent.m_type == Command)
        stream << taskEvent.m_payload;

    sendBuffer(buffer);
}
