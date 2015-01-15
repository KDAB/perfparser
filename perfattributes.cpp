#include "perfattributes.h"
#include "perfdata.h"

#include <QDebug>

PerfEventAttributes::PerfEventAttributes()
{
    memset(this, 0, sizeof(PerfEventAttributes));
}

bool PerfEventAttributes::readFromStream(QDataStream &stream)
{
    quint64 flags;
    stream >> m_type >> m_size;

    if (m_size < sizeof(PerfEventAttributes)) {
        qDebug() << "unsupported file format";
        return false;
    }

    stream >> m_config >> m_samplePeriod >> m_sampleType >> m_readFormat
           >> flags >> m_wakeupEvents >> m_bpType >> m_bpAddr >> m_bpLen >> m_branchSampleType
           >> m_sampleRegsUser >> m_sampleStackUser;

    if (static_cast<QSysInfo::Endian>(stream.byteOrder()) != QSysInfo::ByteOrder) {
        // bit fields are saved in byte order; who came up with that BS?
        quint64 newFlags = 0;
        for (int i = 0; i < 64; ++i) {
            if ((flags & (1ull << i)) != 0)
                newFlags |= (1ull << (i / 8 + 7 - (i % 8)));
        }
        flags = newFlags;
    }

    *(&m_readFormat + 1) = flags;

    qDebug() << sizeof(PerfEventAttributes);
    qDebug() << m_type << m_size << m_config << m_samplePeriod << m_sampleType << m_readFormat
             << flags << m_wakeupEvents << m_bpType << m_bpAddr << m_bpLen << m_branchSampleType
             << m_sampleRegsUser << m_sampleStackUser;

    qDebug() << "flags" << m_disabled << m_inherit << m_pinned << m_exclusive << m_excludeUser
             << m_excludeKernel << m_excludeHv << m_excludeIdle << m_mmap << m_comm << m_freq
             << m_inheritStat << m_enableOnExec << m_task << m_watermark << m_preciseIp
             << m_mmapData << m_sampleIdAll << m_excludeHost << m_excludeGuest << m_reserved1;

    stream.skipRawData(m_size - sizeof(PerfEventAttributes));

    return true;
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
        offset += sizeof(PerfSampleId::pid) + sizeof(PerfSampleId::tid);

    if (m_sampleType & SAMPLE_TIME)
        offset += sizeof(PerfSampleId::time);

    if (m_sampleType & SAMPLE_ADDR)
        offset += sizeof(quint64); // PerfRecordSample::m_addr

    return offset;
}




bool PerfAttributes::read(QIODevice *device, PerfHeader *header)
{
    if (header->attrSize() < sizeof(PerfAttributes)) {
        qWarning() << "unsupported file format";
        return false;
    }


    PerfEventAttributes attrs;
    PerfFileSection ids;

    for (uint i = 0; i < header->numAttrs(); ++i) {

        if (!device->seek(header->attrs().offset + header->attrSize() * i)) {
            qWarning() << "cannot seek to attribute section";
            return false;
        }

        QDataStream stream(device);
        stream.setByteOrder(header->byteOrder());
        if (!attrs.readFromStream(stream))
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
            stream.setByteOrder(header->byteOrder());
            quint64 id;
            qDebug() << "ids" << ids.size << ids.offset;
            for (uint i = 0; i < ids.size / sizeof(quint64); ++i) {
                idStream >> id;
                qDebug() << "assigning to" << id;
                m_attributes[id] = attrs;
            }
        }

    }
    return true;
}

const PerfEventAttributes &PerfAttributes::attributes(quint64 id) const
{
    QHash<quint64, PerfEventAttributes>::ConstIterator i = m_attributes.find(id);
    if (i != m_attributes.end())
        return i.value();
    else
        return m_globalAttributes;
}
