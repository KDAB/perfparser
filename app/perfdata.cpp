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

#include "perfdata.h"
#include "perfunwind.h"

#include <QDebug>
#include <limits>

PerfData::PerfData(QIODevice *source, PerfUnwind *destination, const PerfHeader *header,
                   PerfAttributes *attributes) :
    m_source(source), m_destination(destination), m_header(header), m_attributes(attributes)
{
}

PerfData::ReadStatus PerfData::processEvents(QDataStream &stream)
{
    qint64 headerSize = PerfEventHeader::fixedLength();

    if (m_eventHeader.size == 0) {
        if (stream.device()->bytesAvailable() < headerSize)
            return Rerun;

        stream >> m_eventHeader;

        if (m_eventHeader.size < headerSize) {
            qWarning() << "bad event header size" << m_eventHeader.size << m_eventHeader.type
                       << m_eventHeader.misc;
            return SignalError;
        }
    }

    const qint64 contentSize = m_eventHeader.size - headerSize;
    if (stream.device()->bytesAvailable() < contentSize)
        return Rerun;

    const PerfEventAttributes &attrs = m_attributes->globalAttributes();
    int idOffset = attrs.sampleIdOffset();
    bool sampleIdAll = attrs.sampleIdAll();
    quint64 sampleType = attrs.sampleType();

    switch (m_eventHeader.type) {
    case PERF_RECORD_MMAP: {
        PerfRecordMmap mmap(&m_eventHeader, sampleType, sampleIdAll);
        stream >> mmap;
        m_destination->registerElf(mmap);
        break;
    }
    case PERF_RECORD_LOST: {
        PerfRecordLost lost(&m_eventHeader, sampleType, sampleIdAll);
        stream >> lost;
        m_destination->lost(lost);
        break;
    }
    case PERF_RECORD_COMM: {
        PerfRecordComm comm(&m_eventHeader, sampleType, sampleIdAll);
        stream >> comm;
        m_destination->comm(comm);
        break;
    }
    case PERF_RECORD_SAMPLE: {
        if (sampleIdAll && idOffset >= 0) {
            QByteArray buffer(contentSize, Qt::Uninitialized);
            stream.readRawData(buffer.data(), contentSize);
            QDataStream contentStream(buffer);
            contentStream.setByteOrder(stream.byteOrder());

            Q_ASSERT(!contentStream.device()->isSequential());

            // peek into the data structure to find the actual ID. Horrible.
            quint64 id;
            qint64 prevPos = contentStream.device()->pos();
            contentStream.device()->seek(prevPos + idOffset);
            contentStream >> id;
            contentStream.device()->seek(prevPos);

            PerfRecordSample sample(&m_eventHeader, &m_attributes->attributes(id));
            contentStream >> sample;
            m_destination->sample(sample);
        } else {
            PerfRecordSample sample(&m_eventHeader, &attrs);
            stream >> sample;
            m_destination->sample(sample);
        }

        break;
    }
    case PERF_RECORD_MMAP2: {
        PerfRecordMmap2 mmap2(&m_eventHeader, sampleType, sampleIdAll);
        stream >> mmap2;
        m_destination->registerElf(mmap2); // Throw out the extra data for now.
        break;
    }
    case PERF_RECORD_HEADER_ATTR: {
        PerfRecordAttr attr(&m_eventHeader, sampleType, sampleIdAll);
        stream >> attr;
        m_destination->attr(attr);
        if (m_attributes->globalAttributes().size() == 0)
            m_attributes->setGlobalAttributes(attr.attr());

        foreach (quint64 id, attr.ids())
            m_attributes->addAttributes(id, attr.attr());

        break;
    }
    case PERF_RECORD_FORK: {
        PerfRecordFork fork(&m_eventHeader, sampleType, sampleIdAll);
        stream >> fork;
        m_destination->fork(fork);
        break;
    }
    case PERF_RECORD_EXIT: {
        PerfRecordFork exit(&m_eventHeader, sampleType, sampleIdAll);
        stream >> exit;
        m_destination->exit(exit);
        break;
    }
    case PERF_RECORD_HEADER_TRACING_DATA: {
        if (contentSize == 4) {
            // The content is actually another 4 byte integer,
            // describing the size of the real content that follows.
            quint32 content;
            stream >> content;
            stream.skipRawData(content);
        } else {
            // Maybe someone with a brain will fix this eventually ...
            // then we'll hit this branch.
            qWarning() << "HEADER_TRACING_DATA with unexpected contentSize" << contentSize;
            stream.skipRawData(contentSize);
        }
        break;
    }
    case PERF_RECORD_FINISHED_ROUND: {
        m_destination->finishedRound();
        if (contentSize != 0) {
            qWarning() << "FINISHED_ROUND with non-zero content size detected"
                       << contentSize;
            stream.skipRawData(contentSize);
        }
        break;
    }

    default:
        qWarning() << "unhandled event type" << m_eventHeader.type;
        stream.skipRawData(contentSize);
        break;
    }

    m_eventHeader.size = 0;

    return SignalFinished;
}

PerfData::ReadStatus PerfData::doRead()
{
    QDataStream stream(m_source);
    stream.setByteOrder(m_header->byteOrder());
    ReadStatus returnCode = SignalFinished;

    if (m_header->isPipe()) {
        if (m_source->isSequential()) {
            while (m_source->bytesAvailable() > 0) {
                returnCode = processEvents(stream);
                if (returnCode == SignalError || returnCode == Rerun)
                    break;
            }
            if (returnCode != SignalError) {
                if (m_source->isOpen()) {
                    // finished some event, but not the whole stream
                    returnCode = Rerun;
                } else {
                    // if there is a half event left when the stream finishes, that's bad
                    returnCode = m_eventHeader.size != 0 ? SignalError : SignalFinished;
                }
            }
        } else {
            while (!m_source->atEnd()) {
                if (processEvents(stream) != SignalFinished) {
                    returnCode = SignalError;
                    break;
                }
            }
        }
    } else if (m_source->isSequential()) {
        qWarning() << "cannot read non-stream format from stream";
        returnCode = SignalError;
    } else if (!m_source->seek(m_header->dataOffset())) {
        qWarning() << "cannot seek to" << m_header->dataOffset();
        returnCode = SignalError;
    } else {
        const auto dataOffset = m_header->dataOffset();
        const auto dataSize = m_header->dataSize();
        const auto endOfDataSection = dataOffset + dataSize;

        m_destination->sendProgress(float(m_source->pos() - dataOffset) / dataSize);
        const qint64 posDeltaBetweenProgress = dataSize / 100;
        qint64 nextProgressAt = m_source->pos() + posDeltaBetweenProgress;

        while (static_cast<quint64>(m_source->pos()) < endOfDataSection) {
            if (processEvents(stream) != SignalFinished) {
                returnCode = SignalError;
                break;
            }
            if (m_source->pos() >= nextProgressAt) {
                m_destination->sendProgress(float(m_source->pos() - dataOffset) / dataSize);
                nextProgressAt += posDeltaBetweenProgress;
            }
        }
    }

    return returnCode;
}

void PerfData::read()
{
    ReadStatus returnCode = doRead();
    switch (returnCode) {
    case SignalFinished:
        disconnect(m_source, &QIODevice::readyRead, this, &PerfData::read);
        disconnect(m_source, &QIODevice::aboutToClose, this, &PerfData::finishReading);
        emit finished();
        break;
    case SignalError:
        disconnect(m_source, &QIODevice::readyRead, this, &PerfData::read);
        disconnect(m_source, &QIODevice::aboutToClose, this, &PerfData::finishReading);
        emit error();
        break;
    case Rerun:
        break;
    }
}

void PerfData::finishReading()
{
    disconnect(m_source, &QIODevice::readyRead, this, &PerfData::read);
    disconnect(m_source, &QIODevice::aboutToClose, this, &PerfData::finishReading);

    ReadStatus returnCode = doRead();
    switch (returnCode) {
    case SignalFinished:
        emit finished();
        break;
    case SignalError:
        emit error();
        break;
    case Rerun:
        if (m_eventHeader.size == 0)
            emit finished();
        else
            emit error();
        break;
    }
}

PerfRecordMmap::PerfRecordMmap(PerfEventHeader *header, quint64 sampleType, bool sampleIdAll) :
    PerfRecord(header, sampleType, sampleIdAll), m_pid(0), m_tid(0), m_addr(0), m_len(0), m_pgoff(0)
{
}

QDataStream &PerfRecordMmap::readNumbers(QDataStream &stream)
{
    return stream >> m_pid >> m_tid >> m_addr >> m_len >> m_pgoff;
}

QDataStream &PerfRecordMmap::readFilename(QDataStream &stream, quint64 filenameLength)
{
    if (filenameLength > static_cast<quint64>(std::numeric_limits<int>::max())) {
        qWarning() << "bad filename length";
        return stream;
    }
    m_filename.resize(filenameLength);
    stream.readRawData(m_filename.data(), filenameLength);
    int null = m_filename.indexOf('\0');
    if (null != -1)
        m_filename.truncate(null);
    return stream;
}

QDataStream &PerfRecordMmap::readSampleId(QDataStream &stream)
{
    stream >> m_sampleId;
    return stream;
}

quint64 PerfRecordMmap::fixedLength() const
{
    return sizeof(m_pid) + sizeof(m_tid) + sizeof(m_addr) + sizeof(m_len) + sizeof(m_pgoff)
            + m_header.fixedLength() + m_sampleId.fixedLength();
}

QDataStream &operator>>(QDataStream &stream, PerfRecordMmap &record)
{
    record.readNumbers(stream);
    record.readFilename(stream, record.m_header.size - record.fixedLength());
    record.readSampleId(stream);
    return stream;
}

PerfRecordMmap2::PerfRecordMmap2(PerfEventHeader *header, quint64 sampleType, bool sampleIdAll) :
    PerfRecordMmap(header, sampleType, sampleIdAll), m_maj(0), m_min(0), m_ino(0),
    m_ino_generation(0), m_prot(0), m_flags(0)
{
}

QDataStream &PerfRecordMmap2::readNumbers(QDataStream &stream)
{
    PerfRecordMmap::readNumbers(stream);
    return stream >> m_maj >> m_min >> m_ino >> m_ino_generation >> m_prot >> m_flags;
}

quint64 PerfRecordMmap2::fixedLength() const
{
    return PerfRecordMmap::fixedLength() + sizeof(m_maj) + sizeof(m_min) + sizeof(m_ino)
            + sizeof(m_ino_generation) + sizeof(m_prot) + sizeof(m_flags);
}

QDataStream &operator>>(QDataStream &stream, PerfRecordMmap2 &record)
{
    record.readNumbers(stream);
    record.readFilename(stream, record.m_header.size - record.fixedLength());
    record.readSampleId(stream);
    return stream;
}

PerfRecordComm::PerfRecordComm(PerfEventHeader *header, quint64 sampleType, bool sampleIdAll) :
    PerfRecord(header, sampleType, sampleIdAll), m_pid(0), m_tid(0)
{
}

QDataStream &operator>>(QDataStream &stream, PerfRecordComm &record)
{
    stream >> record.m_pid >> record.m_tid;
    const quint64 commLength = record.m_header.size - record.fixedLength();

    if (commLength > static_cast<quint64>(std::numeric_limits<int>::max())) {
        qWarning() << "bad comm length";
        return stream;
    }
    record.m_comm.resize(commLength);
    stream.readRawData(record.m_comm.data(), commLength);
    int null = record.m_comm.indexOf('\0');
    if (null != -1)
        record.m_comm.truncate(null);

    stream >> record.m_sampleId;

    return stream;
}


PerfRecordLost::PerfRecordLost(PerfEventHeader *header, quint64 sampleType, bool sampleIdAll) :
    PerfRecord(header, sampleType, sampleIdAll), m_id(0), m_lost(0)
{
}


QDataStream &operator>>(QDataStream &stream, PerfRecordLost &record)
{
    stream >> record.m_id >> record.m_lost >> record.m_sampleId;
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfSampleId &sampleId)
{
    if (sampleId.m_sampleType & PerfEventAttributes::SAMPLE_ID_ALL) {
        if (sampleId.m_sampleType & PerfEventAttributes::SAMPLE_TID)
            stream >> sampleId.m_pid >> sampleId.m_tid;
        if (sampleId.m_sampleType & PerfEventAttributes::SAMPLE_TIME)
            stream >> sampleId.m_time;
        if (sampleId.m_sampleType & PerfEventAttributes::SAMPLE_ID)
            stream >> sampleId.m_id;
        if (sampleId.m_sampleType & PerfEventAttributes::SAMPLE_STREAM_ID)
            stream >> sampleId.m_streamId;
        if (sampleId.m_sampleType & PerfEventAttributes::SAMPLE_CPU)
            stream >> sampleId.m_res >> sampleId.m_cpu;
        if (sampleId.m_sampleType & PerfEventAttributes::SAMPLE_IDENTIFIER)
            stream.skipRawData(sizeof(sampleId.m_ignoredDuplicateId));
    }
    return stream;
}


quint64 PerfSampleId::fixedLength() const
{
    quint64 ret = 0;
    if (m_sampleType & PerfEventAttributes::SAMPLE_ID_ALL) {
        if (m_sampleType & PerfEventAttributes::SAMPLE_TID)
            ret += sizeof(m_pid) + sizeof(m_tid);
        if (m_sampleType & PerfEventAttributes::SAMPLE_TIME)
            ret += sizeof(m_time);
        if (m_sampleType & PerfEventAttributes::SAMPLE_ID)
            ret += sizeof(m_id);
        if (m_sampleType & PerfEventAttributes::SAMPLE_STREAM_ID)
            ret += sizeof(m_streamId);
        if (m_sampleType & PerfEventAttributes::SAMPLE_CPU)
            ret += sizeof(m_res) + sizeof(m_cpu);
        if (m_sampleType & PerfEventAttributes::SAMPLE_IDENTIFIER)
            ret += sizeof(m_ignoredDuplicateId);
    }
    return ret;
}


PerfRecord::PerfRecord(const PerfEventHeader *header, quint64 sampleType, bool sampleIdAll) :
    m_header(header ? *header : PerfEventHeader()), m_sampleId(sampleType, sampleIdAll)
{
}


PerfRecordSample::PerfRecordSample(const PerfEventHeader *header,
                                   const PerfEventAttributes *attributes)
    : PerfRecord(header, attributes->sampleType(), false), m_readFormat(attributes->readFormat()),
      m_registerMask(attributes->sampleRegsUser()), m_ip(0), m_addr(0), m_period(0),
      m_timeEnabled(0), m_timeRunning(0), m_registerAbi(0)
{
}

quint64 PerfRecordSample::registerValue(uint reg) const
{
    Q_ASSERT(m_registerAbi && m_registerMask & (1ull << reg));

    int index = 0;
    for (uint i = 0; i < reg; i++) {
        if (m_registerMask & (1ull << i))
            index++;
    }

    if (index < m_registers.length()) {
        return m_registers[index];
    } else {
        qWarning() << "invalid register offset" << index;
        return -1;
    }
}

QDataStream &operator>>(QDataStream &stream, PerfRecordSample &record)
{
    quint32 waste32;

    const quint64 sampleType = record.m_sampleId.sampleType();

    if (sampleType & PerfEventAttributes::SAMPLE_IDENTIFIER)
        stream >> record.m_sampleId.m_id;
    if (sampleType & PerfEventAttributes::SAMPLE_IP)
        stream >> record.m_ip;
    if (sampleType & PerfEventAttributes::SAMPLE_TID)
        stream >> record.m_sampleId.m_pid >> record.m_sampleId.m_tid;
    if (sampleType & PerfEventAttributes::SAMPLE_TIME)
        stream >> record.m_sampleId.m_time;
    if (sampleType & PerfEventAttributes::SAMPLE_ADDR)
        stream >> record.m_addr;
    if (sampleType & PerfEventAttributes::SAMPLE_ID)
        stream >> record.m_sampleId.m_id; // It's the same as identifier
    if (sampleType & PerfEventAttributes::SAMPLE_STREAM_ID)
        stream >> record.m_sampleId.m_streamId;
    if (sampleType & PerfEventAttributes::SAMPLE_CPU)
        stream >> record.m_sampleId.m_cpu >> waste32;
    if (sampleType & PerfEventAttributes::SAMPLE_PERIOD)
        stream >> record.m_period;

    if (sampleType & PerfEventAttributes::SAMPLE_READ) {
        quint64 numFormats = 1;
        PerfRecordSample::ReadFormat format;
        if (record.m_readFormat & PerfEventAttributes::FORMAT_GROUP)
            stream >> numFormats;
        else
            stream >> format.value;

        if (record.m_readFormat & PerfEventAttributes::FORMAT_TOTAL_TIME_ENABLED)
            stream >> record.m_timeEnabled;
        if (record.m_readFormat & PerfEventAttributes::FORMAT_TOTAL_TIME_RUNNING)
            stream >> record.m_timeRunning;

        if (record.m_readFormat & PerfEventAttributes::FORMAT_GROUP) {
            while (numFormats-- > 0) {
                stream >> format.value >> format.id;
                record.m_readFormats << format;
            }
        } else {
            stream >> format.id;
            record.m_readFormats << format;
        }
    }

    if (sampleType & PerfEventAttributes::SAMPLE_CALLCHAIN) {
        quint64 numIps;
        quint64 ip;
        stream >> numIps;
        while (numIps-- > 0) {
            stream >> ip;
            record.m_callchain << ip;
        }
    }

    if (sampleType & PerfEventAttributes::SAMPLE_RAW) {
        quint32 rawSize;
        stream >> rawSize;
        if (rawSize > static_cast<quint32>(std::numeric_limits<int>::max())) {
            qWarning() << "bad raw data section";
            return stream;
        }
        record.m_rawData.resize(rawSize);
        stream.readRawData(record.m_rawData.data(), rawSize);
    }

    if (sampleType & PerfEventAttributes::SAMPLE_BRANCH_STACK) {
        quint64 numBranches;
        stream >> numBranches;
        PerfRecordSample::BranchEntry entry;
        while (numBranches-- > 0) {
            stream >> entry.from >> entry.to;
            record.m_branchStack << entry;
        }
    }

    if (sampleType & PerfEventAttributes::SAMPLE_REGS_USER) {
        quint64 reg;
        stream >> record.m_registerAbi;
        if (record.m_registerAbi) {
            for (uint i = qPopulationCount(record.m_registerMask); i > 0; --i) {
                stream >> reg;
                record.m_registers << reg;
            }
        }
    }

    if (sampleType & PerfEventAttributes::SAMPLE_STACK_USER) {
        quint64 size;
        stream >> size;

        if (size > static_cast<quint64>(std::numeric_limits<int>::max())) {
            // We don't accept stack samples of > 2G, sorry ...
            qWarning() << "bad stack size";
            return stream;
        }
        if (size > 0) {
            record.m_userStack.resize(size);
            stream.readRawData(record.m_userStack.data(), size);
            stream >> size;
        }
    }

    if (sampleType & PerfEventAttributes::SAMPLE_WEIGHT)
        stream >> record.m_weight;

    if (sampleType & PerfEventAttributes::SAMPLE_DATA_SRC)
        stream >> record.m_dataSrc;

    if (sampleType & PerfEventAttributes::SAMPLE_TRANSACTION)
        stream >> record.m_transaction;

    return stream;
}


PerfRecordAttr::PerfRecordAttr(const PerfEventHeader *header, quint64 sampleType, bool sampleIdAll) :
    PerfRecord(header, sampleType, sampleIdAll)
{
}

PerfRecordAttr::PerfRecordAttr(const PerfEventAttributes &attributes, const QList<quint64> &ids) :
    PerfRecord(nullptr, 0, false), m_attr(attributes), m_ids(ids)
{
}

QDataStream &operator>>(QDataStream &stream, PerfRecordAttr &record)
{
    stream >> record.m_attr;
    quint32 read = record.m_attr.size() + PerfEventHeader::fixedLength();
    quint64 id = 0;
    for (quint32 i = 0; i < (record.m_header.size - read) / sizeof(quint64); ++i) {
        stream >> id;
        record.m_ids << id;
    }
    return stream;
}


PerfRecordFork::PerfRecordFork(PerfEventHeader *header, quint64 sampleType, bool sampleIdAll) :
    PerfRecord(header, sampleType, sampleIdAll), m_pid(0), m_ppid(0), m_tid(0), m_ptid(0), m_time(0)
{
}

QDataStream &operator>>(QDataStream &stream, PerfRecordFork &record)
{
    return stream >> record.m_pid >> record.m_ppid >> record.m_tid >> record.m_ptid >> record.m_time
                  >> record.m_sampleId;
}
