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

#include "perffeatures.h"

#include <QDebug>

#include <limits>

// TODO: What to do if feature flags are set but features don't really exist in the file?

static void removeTrailingZeros(QByteArray *string)
{
    int length = string->length();
    // chop off trailing zeros to make the values directly usable
    while (length > 0 && !string->at(length - 1))
        --length;
    string->resize(length);
}

void PerfFeatures::createFeature(QIODevice *device, QDataStream::ByteOrder byteOrder,
                                 const PerfFileSection &section, PerfHeader::Feature featureId)
{
    device->seek(section.offset);
    QDataStream stream(device);
    stream.setByteOrder(byteOrder);

    switch (featureId) {
    case PerfHeader::TRACING_DATA:
        if (section.size > std::numeric_limits<quint32>::max()) {
            qWarning() << "Excessively large tracing data section" << section.size;
        } else if (section.size < 0) {
            qWarning() << "Tracing data section with negative size" << section.size;
        } else {
            m_tracingData.setSize(static_cast<quint32>(section.size));
            stream >> m_tracingData;
        }
        break;
    case PerfHeader::BUILD_ID:
        m_buildId.size = section.size;
        stream >> m_buildId;
        break;
    case PerfHeader::HOSTNAME:
        stream >> m_hostName;
        break;
    case PerfHeader::OSRELEASE:
        stream >> m_osRelease;
        break;
    case PerfHeader::VERSION:
        stream >> m_version;
        break;
    case PerfHeader::ARCH:
        stream >> m_arch;
        break;
    case PerfHeader::CPUDESC:
        stream >> m_cpuDesc;
        break;
    case PerfHeader::CPUID:
        stream >> m_cpuId;
        break;
    case PerfHeader::NRCPUS:
        stream >> m_nrCpus;
        break;
    case PerfHeader::TOTAL_MEM:
        stream >> m_totalMem;
        break;
    case PerfHeader::CMDLINE:
        stream >> m_cmdline;
        break;
    case PerfHeader::EVENT_DESC:
        stream >> m_eventDesc;
        break;
    case PerfHeader::CPU_TOPOLOGY:
        stream >> m_cpuTopology;
        break;
    case PerfHeader::NUMA_TOPOLOGY:
        stream >> m_numaToplogy;
        break;
    case PerfHeader::BRANCH_STACK:
        // Doesn't really exist
        break;
    case PerfHeader::PMU_MAPPINGS:
        stream >> m_pmuMappings;
        break;
    case PerfHeader::GROUP_DESC:
        stream >> m_groupDesc;
        break;
    default:
        break;
    }

    qint64 readSize = device->pos() - section.offset;
    if (section.size != readSize)
        qWarning() << "feature not properly read" << featureId << section.size << readSize;
}

PerfFeatures::PerfFeatures()
{
}

PerfFeatures::~PerfFeatures()
{
}

bool PerfFeatures::read(QIODevice *device, const PerfHeader *header)
{
    if (!device->seek(header->featureOffset())) {
        qWarning() << "cannot seek to" << header->featureOffset();
        return false;
    }
    QDataStream stream(device);
    stream.setByteOrder(header->byteOrder());

    QHash<PerfHeader::Feature, PerfFileSection> featureSections;
    PerfFileSection section;
    for (uint i = 0; i < PerfHeader::LAST_FEATURE; ++i) {
        PerfHeader::Feature feature = static_cast<PerfHeader::Feature>(i);
        if (header->hasFeature(feature)) {
            stream >> section;
            if (section.size > 0)
                featureSections[feature] = section;
            else
                qWarning() << "Feature announced in header but not present:" << feature;
        }
    }

    QHash<PerfHeader::Feature, PerfFileSection>::ConstIterator i;
    for (i = featureSections.constBegin(); i != featureSections.constEnd(); ++i)
        createFeature(device, stream.byteOrder(), i.value(), i.key());

    return true;
}

QDataStream &operator>>(QDataStream &stream, PerfBuildId &buildId)
{
    qint64 next = 0;
    while (next < buildId.size) {
        PerfEventHeader header;
        stream >> header;

        PerfBuildId::BuildId build;
        stream >> build.pid;

        build.id.resize(PerfBuildId::s_idLength);
        stream.readRawData(build.id.data(), PerfBuildId::s_idLength);
        stream.skipRawData(PerfBuildId::s_idPadding);

        quint16 fileNameLength = header.size - PerfEventHeader::fixedLength() - sizeof(build.pid)
                - PerfBuildId::s_idPadding - PerfBuildId::s_idLength;
        build.fileName.resize(fileNameLength);
        stream.readRawData(build.fileName.data(), fileNameLength);
        removeTrailingZeros(&build.fileName);

        next += header.size;
        buildId.buildIds << build;
    }
    return stream;
}

QDataStream &operator<<(QDataStream &stream, const PerfBuildId::BuildId &buildId)
{
    return stream << buildId.pid << buildId.id << buildId.fileName;
}

QDataStream &operator>>(QDataStream &stream, PerfEventHeader &header)
{
    return stream >> header.type >> header.misc >> header.size;
}

QDataStream &operator>>(QDataStream &stream, PerfStringFeature &string)
{
    quint32 length;
    stream >> length;
    static const int intMax = std::numeric_limits<int>::max();
    if (length > intMax) {
        qWarning() << "Excessively long string" << length;
        stream.skipRawData(intMax);
        stream.skipRawData(static_cast<int>(length - intMax));
        return stream;
    }
    string.value.resize(static_cast<int>(length));
    stream.readRawData(string.value.data(), string.value.length());
    removeTrailingZeros(&string.value);
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfNrCpus &nrCpus)
{
    return stream >> nrCpus.online >> nrCpus.available;
}

QDataStream &operator<<(QDataStream &stream, const PerfNrCpus &nrCpus)
{
    return stream << nrCpus.online << nrCpus.available;
}

QDataStream &operator>>(QDataStream &stream, PerfTotalMem &totalMem)
{
    return stream >> totalMem.totalMem;
}

QDataStream &operator>>(QDataStream &stream, PerfCmdline &cmdline)
{
    quint32 numParts;
    stream >> numParts;
    PerfStringFeature stringFeature;
    while (numParts-- > 0) {
        stream >> stringFeature;
        cmdline.cmdline << stringFeature.value;
    }
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfEventDesc &eventDesc)
{
    quint32 numEvents;
    quint32 eventSize;
    quint32 numIds;
    quint64 id;
    stream >> numEvents >> eventSize;
    PerfStringFeature stringFeature;
    while (numEvents-- > 0) {
        eventDesc.eventDescs << PerfEventDesc::EventDesc();
        PerfEventDesc::EventDesc &currentEvent = eventDesc.eventDescs.last();
        stream >> currentEvent.attrs;
        stream >> numIds;
        stream >> stringFeature;
        currentEvent.name = stringFeature.value;
        while (numIds-- > 0) {
            stream >> id;
            currentEvent.ids << id;
        }
    }
    // There is some additional length-encoded stuff after this, but perf doesn't read that, either.
    // On top of that perf is only interested in the event name and throws everything else away
    // after reading.
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfCpuTopology &cpuTopology)
{
    quint32 numSiblings;
    PerfStringFeature stringFeature;
    stream >> numSiblings;

    while (numSiblings-- > 0) {
        stream >> stringFeature;
        cpuTopology.siblingCores << stringFeature.value;
    }

    stream >> numSiblings;
    while (numSiblings-- > 0) {
        stream >> stringFeature;
        cpuTopology.siblingThreads << stringFeature.value;
    }
    return stream;
}

QDataStream &operator<<(QDataStream &stream, const PerfCpuTopology &cpuTopology)
{
    return stream << cpuTopology.siblingCores << cpuTopology.siblingThreads;
}

QDataStream &operator>>(QDataStream &stream, PerfNumaTopology &numaTopology)
{
    quint32 numNodes;
    stream >> numNodes;

    PerfStringFeature stringFeature;
    while (numNodes-- > 0) {
        PerfNumaTopology::NumaNode node;
        stream >> node.nodeId >> node.memTotal >> node.memFree >> stringFeature;
        node.topology = stringFeature.value;
        numaTopology.nodes << node;
    }
    return stream;
}

QDataStream &operator<<(QDataStream &stream, const PerfNumaTopology::NumaNode &node)
{
    return stream << node.nodeId << node.memTotal << node.memFree << node.topology;
}

QDataStream &operator>>(QDataStream &stream, PerfPmuMappings &pmuMappings)
{
    quint32 numPmus;
    stream >> numPmus;

    PerfStringFeature stringFeature;
    while (numPmus-- > 0) {
        PerfPmuMappings::Pmu pmu;
        stream >> pmu.type >> stringFeature;
        pmu.name = stringFeature.value;
        pmuMappings.pmus << pmu;
    }
    return stream;
}

QDataStream &operator<<(QDataStream &stream, const PerfPmuMappings::Pmu &pmu)
{
    return stream << pmu.type << pmu.name;
}

QDataStream &operator>>(QDataStream &stream, PerfGroupDesc &groupDesc)
{
    quint32 numGroups;
    stream >> numGroups;

    PerfStringFeature stringFeature;
    while (numGroups-- > 0) {
        PerfGroupDesc::GroupDesc desc;
        stream >> stringFeature;
        desc.name = stringFeature.value;
        stream >> desc.leaderIndex >> desc.numMembers;
        groupDesc.groupDescs << desc;
    }
    return stream;
}

QDataStream &operator<<(QDataStream &stream, const PerfGroupDesc::GroupDesc &groupDesc)
{
    return stream << groupDesc.name << groupDesc.leaderIndex << groupDesc.numMembers;
}
