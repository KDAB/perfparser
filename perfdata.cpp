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
#include <QDebug>
#include <limits>

PerfData::PerfData()
{
}

bool PerfData::processEvent(QDataStream &stream, PerfAttributes *attributes)
{

    qint64 headerSize = sizeof(PerfEventHeader);

    PerfEventHeader eventHeader;
    quint64 pos = stream.device()->pos();
    stream >> eventHeader;

    if (eventHeader.size < headerSize) {
        qWarning() << "bad event header size" << eventHeader.size << eventHeader.type
                   << eventHeader.misc;
        return false;
    }

    const PerfEventAttributes &attrs = attributes->globalAttributes();
    int idOffset = attrs.sampleIdOffset();
    bool sampleIdAll = attrs.sampleIdAll();
    quint64 sampleType = attrs.sampleType();

    switch (eventHeader.type) {
    case PERF_RECORD_MMAP:
        m_mmapRecords << PerfRecordMmap(&eventHeader, sampleType, sampleIdAll);
        stream >> m_mmapRecords.last();
        break;
    case PERF_RECORD_LOST:
        m_lostRecords << PerfRecordLost(&eventHeader, sampleType, sampleIdAll);
        stream >> m_lostRecords.last();
        break;
    case PERF_RECORD_COMM:
        m_commRecords << PerfRecordComm(&eventHeader, sampleType, sampleIdAll);
        stream >> m_commRecords.last();
        break;
    case PERF_RECORD_SAMPLE: {
        const PerfEventAttributes *sampleAttrs = &attrs;

        if (sampleIdAll && idOffset >= 0) {
            // peek into the data structure to find the actual ID. Horrible.
            quint64 id;
            qint64 prevPos = stream.device()->pos();
            stream.device()->seek(prevPos + idOffset);
            stream >> id;
            stream.device()->seek(prevPos);
            sampleAttrs = &attributes->attributes(id);
        }

        // TODO: for this we have to find the right attribute by some kind of hash and id ...
        m_sampleRecords << PerfRecordSample(&eventHeader, sampleAttrs);
        stream >> m_sampleRecords.last();
        break;
    }
    case PERF_RECORD_MMAP2: {
        PerfRecordMmap2 mmap2(&eventHeader, sampleType, sampleIdAll);
        stream >> mmap2;
        m_mmapRecords << mmap2; // Throw out the extra data for now.
        break;
    }
    case PERF_RECORD_HEADER_ATTR: {
        PerfRecordAttr attr(&eventHeader, sampleType, sampleIdAll);
        stream >> attr;
        if (attributes->globalAttributes().size() == 0)
            attributes->setGlobalAttributes(attr.attr());

        foreach (quint64 id, attr.ids())
            attributes->addAttributes(id, attr.attr());

        break;
    }

    default:
        qWarning() << "unhandled event type" << eventHeader.type;
        stream.skipRawData(eventHeader.size - headerSize);
        break;
    }

    // Read wrong number of bytes => something broken
    if (stream.device()->pos() - pos != eventHeader.size) {
        qWarning() << "While parsing event of type" << eventHeader.type;
        qWarning() << "read" << (stream.device()->pos() - pos) << "instead of"
                   << eventHeader.size << "bytes";
        return false;
    }

    return true;
}

bool PerfData::read(QIODevice *device, const PerfHeader *header, PerfAttributes *attributes)
{
    QDataStream stream(device);
    stream.setByteOrder(header->byteOrder());

    if (header->isPipe()) {
        while (!stream.atEnd()) {
            if (!processEvent(stream, attributes))
                return false;
        }
    } else {
        if (!device->seek(header->dataOffset())) {
            qWarning() << "cannot seek to" << header->dataOffset();
            return false;
        }

        while (static_cast<quint64>(device->pos()) < header->dataOffset() + header->dataSize()) {
            if (!processEvent(stream, attributes))
                return false;
        }
    }

    return true;
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
    return stream;
}

QDataStream &PerfRecordMmap::readSampleId(QDataStream &stream)
{
    stream >> m_sampleId;

    if (m_sampleId.sampleType() &
            (PerfEventAttributes::SAMPLE_ID_ALL | PerfEventAttributes::SAMPLE_TID)) {
        if (m_sampleId.pid() != m_pid)
            qWarning() << "ambiguous pids in mmap event" << m_sampleId.pid() << m_pid;
        if (m_sampleId.tid() != m_tid)
            qWarning() << "ambiguous tids in mmap event" << m_sampleId.tid() << m_tid;
    }

    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfRecordMmap &record)
{
    record.readNumbers(stream);
    quint64 filenameLength = record.m_header.size - sizeof(PerfRecordMmap) + sizeof(PerfSampleId) +
            sizeof(record.m_filename) - record.m_sampleId.length();
    record.readFilename(stream, filenameLength);
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

QDataStream &operator>>(QDataStream &stream, PerfRecordMmap2 &record)
{
    record.readNumbers(stream);
    quint64 filenameLength = record.m_header.size - sizeof(PerfRecordMmap2) + sizeof(PerfSampleId) +
            sizeof(record.m_filename) - record.m_sampleId.length();
    record.readFilename(stream, filenameLength);
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
    quint64 commLength = record.m_header.size - sizeof(PerfRecordComm) + sizeof(PerfSampleId) +
            sizeof(record.m_comm) - record.m_sampleId.length();

    if (commLength > static_cast<quint64>(std::numeric_limits<int>::max())) {
        qWarning() << "bad comm length";
        return stream;
    }
    record.m_comm.resize(commLength);
    stream.readRawData(record.m_comm.data(), commLength);
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


quint64 PerfSampleId::length() const
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
    Q_ASSERT(m_registerAbi && m_registerMask & (1 << reg));

    int index = 0;
    for (uint i = 0; i < reg; i++) {
        if (m_registerMask & (1 << i))
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


QDataStream &operator>>(QDataStream &stream, PerfRecordAttr &record)
{
    qint64 pos = stream.device()->pos();
    stream >> record.m_attr;
    qint64 read = stream.device()->pos() - pos + sizeof(PerfEventHeader);
    quint64 id = 0;
    for (quint64 i = 0; i < (record.m_header.size - read) / sizeof(quint64); ++i) {
        stream >> id;
        record.m_ids << id;
    }
    return stream;
}
