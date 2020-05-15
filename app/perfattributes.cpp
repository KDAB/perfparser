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

#include "perfattributes.h"
#include "perfdata.h"

#include <QDebug>

PerfEventAttributes::PerfEventAttributes()
{
    memset(this, 0, sizeof(PerfEventAttributes));
}

QDataStream &operator>>(QDataStream &stream, PerfEventAttributes &attrs)
{
    quint64 flags;
    stream >> attrs.m_type >> attrs.m_size;

    if (attrs.m_size < PerfEventAttributes::SIZE_VER0) {
        qWarning() << "unsupported file format. event attr size too small:" << attrs.m_size;
        return stream;
    }

    stream >> attrs.m_config >> attrs.m_samplePeriod >> attrs.m_sampleType >> attrs.m_readFormat
           >> flags >> attrs.m_wakeupEvents >> attrs.m_bpType >> attrs.m_bpAddr;

    if (attrs.m_size > PerfEventAttributes::SIZE_VER0)
        stream >> attrs.m_bpLen;

    if (attrs.m_size > PerfEventAttributes::SIZE_VER1)
        stream >> attrs.m_branchSampleType;

    if (attrs.m_size > PerfEventAttributes::SIZE_VER2)
        stream >> attrs.m_sampleRegsUser >> attrs.m_sampleStackUser >> attrs.m_clockid;

    if (attrs.m_size > PerfEventAttributes::SIZE_VER3)
        stream >> attrs.m_sampleRegsIntr;

    if (attrs.m_size > PerfEventAttributes::SIZE_VER4)
        stream >> attrs.m_auxWatermark >> attrs.m_sampleMaxStack >> attrs.m_reserved2;

    if (static_cast<QSysInfo::Endian>(stream.byteOrder()) != QSysInfo::ByteOrder) {
        // bit fields are saved in byte order; who came up with that BS?
        quint64 newFlags = 0;
        for (int i = 0; i < 64; ++i) {
            if ((flags & (1ull << i)) != 0)
                newFlags |= (1ull << (i / 8 + 7 - (i % 8)));
        }
        flags = newFlags;
    }

    *(&attrs.m_readFormat + 1) = flags;

    if (attrs.m_size > PerfEventAttributes::SIZE_VER5) {
        static const int intMax = std::numeric_limits<int>::max();
        quint32 skip = attrs.m_size - PerfEventAttributes::SIZE_VER5;
        if (skip > intMax) {
            stream.skipRawData(intMax);
            skip -= intMax;
        }
        stream.skipRawData(static_cast<int>(skip));
    }

    return stream;
}

int PerfEventAttributes::sampleIdOffset() const
{
    int offset = 0;

    if (m_sampleType & SAMPLE_IDENTIFIER)
        return 0;

    if (!(m_sampleType & SAMPLE_ID))
        return -1;

    if (m_sampleType & SAMPLE_IP)
        offset += sizeof(quint64); // PerfRecordSample::m_ip

    if (m_sampleType & SAMPLE_TID)
        offset += sizeof(quint32) + sizeof(quint32); // PerfRecordSampleId::{m_pid|m_tid}

    if (m_sampleType & SAMPLE_TIME)
        offset += sizeof(quint64); // PerfSampleId::m_time

    if (m_sampleType & SAMPLE_ADDR)
        offset += sizeof(quint64); // PerfRecordSample::m_addr

    return offset;
}

QByteArray PerfEventAttributes::name() const
{
    switch (m_type) {
    case TYPE_HARDWARE: {
        switch (m_config) {
        case HARDWARE_CPU_CYCLES:              return QByteArrayLiteral("cpu-cycles");
        case HARDWARE_INSTRUCTIONS:            return QByteArrayLiteral("instructions");
        case HARDWARE_CACHE_REFERENCES:        return QByteArrayLiteral("cache-references");
        case HARDWARE_CACHE_MISSES:            return QByteArrayLiteral("cache-misses");
        case HARDWARE_BRANCH_INSTRUCTIONS:     return QByteArrayLiteral("branch-instructions");
        case HARDWARE_BRANCH_MISSES:           return QByteArrayLiteral("branch-misses");
        case HARDWARE_BUS_CYCLES:              return QByteArrayLiteral("bus-cycles");
        case HARDWARE_STALLED_CYCLES_FRONTEND: return QByteArrayLiteral("stalled-cycles-frontend");
        case HARDWARE_STALLED_CYCLES_BACKEND:  return QByteArrayLiteral("stalled-cycles-backend");
        case HARDWARE_REF_CPU_CYCLES:          return QByteArrayLiteral("ref-cycles");
        default: return QByteArrayLiteral("hardware event: 0x") + QByteArray::number(m_config, 16);
        }
    }
    case TYPE_SOFTWARE: {
        switch (m_config) {
        case SOFTWARE_CPU_CLOCK:        return QByteArrayLiteral("cpu-clock");
        case SOFTWARE_TASK_CLOCK:       return QByteArrayLiteral("task-clock");
        case SOFTWARE_PAGE_FAULTS:      return QByteArrayLiteral("page-faults");
        case SOFTWARE_CONTEXT_SWITCHES: return QByteArrayLiteral("context-switches");
        case SOFTWARE_CPU_MIGRATIONS:   return QByteArrayLiteral("cpu-migrations");
        case SOFTWARE_PAGE_FAULTS_MIN:  return QByteArrayLiteral("minor-faults");
        case SOFTWARE_PAGE_FAULTS_MAJ:  return QByteArrayLiteral("major-faults");
        case SOFTWARE_ALIGNMENT_FAULTS: return QByteArrayLiteral("alignment-faults");
        case SOFTWARE_EMULATION_FAULTS: return QByteArrayLiteral("emulation-faults");
        case SOFTWARE_DUMMY:            return QByteArrayLiteral("dummy");
        default: return QByteArrayLiteral("software event: 0x") + QByteArray::number(m_config, 16);
        }
    }
    case TYPE_TRACEPOINT:
        return QByteArrayLiteral("tracepoint: 0x") + QByteArray::number(m_config, 16);
    case TYPE_HARDWARE_CACHE: {
        QByteArray result;
        switch (m_config & 0xff) {
        case HARDWARE_CACHE_L1D:  result += QByteArrayLiteral("L1-dcache"); break;
        case HARDWARE_CACHE_L1I:  result += QByteArrayLiteral("L1-icache"); break;
        case HARDWARE_CACHE_LL:   result += QByteArrayLiteral("LLC");       break;
        case HARDWARE_CACHE_DTLB: result += QByteArrayLiteral("dTLB");      break;
        case HARDWARE_CACHE_ITLB: result += QByteArrayLiteral("iTLB");      break;
        case HARDWARE_CACHE_BPU:  result += QByteArrayLiteral("branch");    break;
        case HARDWARE_CACHE_NODE: result += QByteArrayLiteral("node");      break;
        default: return QByteArrayLiteral("hardware cache event: 0x")
                    + QByteArray::number(m_config, 16);
        }
        switch ((m_config >> 8) & 0xff) {
        case HARDWARE_CACHE_OPERATION_READ:     result += QByteArrayLiteral("-load");     break;
        case HARDWARE_CACHE_OPERATION_WRITE:    result += QByteArrayLiteral("-store");    break;
        case HARDWARE_CACHE_OPERATION_PREFETCH: result += QByteArrayLiteral("-prefetch"); break;
        default: return result + QByteArrayLiteral(" event: 0x") + QByteArray::number(m_config, 16);
        }
        switch ((m_config >> 16) & 0xff) {
        case HARDWARE_CACHE_RESULT_OPERATION_ACCESS: return result + QByteArrayLiteral("-refs");
        case HARDWARE_CACHE_RESULT_OPERATION_MISS:   return result + QByteArrayLiteral("-misses");
        default: return result + QByteArrayLiteral(" event: 0x") + QByteArray::number(m_config, 16);
        };
    }
    case TYPE_RAW:
        return QByteArrayLiteral("raw event: 0x") + QByteArray::number(m_config, 16);
    case TYPE_BREAKPOINT:
        return QByteArrayLiteral("breakpoint: 0x") + QByteArray::number(m_config, 16);
    default:
        return QByteArrayLiteral("unknown event ") + QByteArray::number(m_type)
                + QByteArrayLiteral(": 0x") + QByteArray::number(m_config, 16);
    }
}

bool PerfEventAttributes::operator==(const PerfEventAttributes &rhs) const
{
    return m_type == rhs.m_type
        && m_size == rhs.m_size
        && m_config == rhs.m_config
        && m_samplePeriod == rhs.m_samplePeriod
        && m_sampleType == rhs.m_sampleType
        && m_readFormat == rhs.m_readFormat
        && m_disabled == rhs.m_disabled
        && m_inherit == rhs.m_inherit
        && m_pinned == rhs.m_pinned
        && m_exclusive == rhs.m_exclusive
        && m_excludeUser == rhs.m_excludeUser
        && m_excludeKernel == rhs.m_excludeKernel
        && m_excludeHv == rhs.m_excludeHv
        && m_excludeIdle == rhs.m_excludeIdle
        && m_mmap == rhs.m_mmap
        && m_comm == rhs.m_comm
        && m_freq == rhs.m_freq
        && m_inheritStat == rhs.m_inheritStat
        && m_enableOnExec == rhs.m_enableOnExec
        && m_task == rhs.m_task
        && m_watermark == rhs.m_watermark
        && m_preciseIp == rhs.m_preciseIp
        && m_mmapData == rhs.m_mmapData
        && m_sampleIdAll == rhs.m_sampleIdAll
        && m_excludeHost == rhs.m_excludeHost
        && m_excludeGuest == rhs.m_excludeGuest
        && m_reserved1 == rhs.m_reserved1
        && m_wakeupEvents == rhs.m_wakeupEvents
        && m_bpType == rhs.m_bpType
        && m_bpAddr == rhs.m_bpAddr
        && m_bpLen == rhs.m_bpLen
        && m_branchSampleType == rhs.m_branchSampleType
        && m_sampleRegsUser == rhs.m_sampleRegsUser
        && m_sampleStackUser == rhs.m_sampleStackUser
        && m_clockid == rhs.m_clockid
        && m_sampleRegsIntr == rhs.m_sampleRegsIntr
        && m_auxWatermark == rhs.m_auxWatermark
        && m_sampleMaxStack == rhs.m_sampleMaxStack
        && m_reserved2 == rhs.m_reserved2;
}

bool PerfAttributes::read(QIODevice *device, PerfHeader *header)
{
    if (header->attrSize() < PerfEventAttributes::SIZE_VER0 + PerfFileSection::fixedLength()) {
        qWarning() << "unsupported file format: header attrSize too small:" << header->attrSize();
        return false;
    }

    PerfEventAttributes attrs;
    PerfFileSection ids;

    for (uint i = 0; i < header->numAttrs(); ++i) {

        if (!device->seek(header->attrs().offset + header->attrSize() * i)) {
            qWarning() << "cannot seek to attribute section" << i
                       << header->attrs().offset + header->attrSize() * i;
            return false;
        }

        QDataStream stream(device);
        stream.setByteOrder(header->byteOrder());
        stream >> attrs;

        if (attrs.size() < PerfEventAttributes::SIZE_VER0)
            return false;

        if (i == 0)
            m_globalAttributes = attrs;

        stream >> ids;
        if (ids.size > 0) {
            if (!device->seek(ids.offset)) {
                qWarning() << "cannot seek to attribute ID section";
                return false;
            }

            QDataStream idStream(device);
            idStream.setByteOrder(header->byteOrder());
            quint64 id;
            for (qint64 i = 0, num = ids.size / static_cast<qint64>(sizeof(quint64));
                 i < num; ++i) {
                idStream >> id;
                m_attributes[id] = attrs;
            }
        }

    }
    return true;
}

void PerfAttributes::setGlobalAttributes(const PerfEventAttributes &attributes)
{
    m_globalAttributes = attributes;
}

void PerfAttributes::addAttributes(quint64 id, const PerfEventAttributes &attributes)
{
    m_attributes[id] = attributes;
}

const PerfEventAttributes &PerfAttributes::attributes(quint64 id) const
{
    QHash<quint64, PerfEventAttributes>::ConstIterator i = m_attributes.find(id);
    if (i != m_attributes.end())
        return i.value();
    else
        return m_globalAttributes;
}
