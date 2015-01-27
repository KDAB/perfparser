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

void PerfFeatures::createFeature(QIODevice *device, QDataStream::ByteOrder byteOrder,
                                 const PerfFileSection &section, PerfHeader::Feature featureId)
{
    device->seek(section.offset);
    QDataStream stream(device);
    stream.setByteOrder(byteOrder);

    qDebug() << featureId << section.offset << section.size;
    switch (featureId) {
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
        stream >> m_branchStack;
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

    quint64 readSize = device->pos() - section.offset;
    if (section.size != readSize) {
        qWarning() << "feature not properly read" << section.size << readSize;
        QByteArray data;
        data.resize(readSize);
        stream.readRawData(data.data(), readSize);
        qDebug() << "remaining data" << data.toHex();
    }
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
        qDebug() << "cannot seek to" << header->featureOffset();
        return false;
    }
    QDataStream stream(device);
    stream.setByteOrder(header->byteOrder());

    QHash<PerfHeader::Feature, PerfFileSection> featureSections;
    PerfFileSection section;
    for (uint i = 0; i < PerfHeader::LAST_FEATURE; ++i) {
        PerfHeader::Feature feature = (PerfHeader::Feature)i;
        if (header->hasFeature(feature)) {
            stream >> section;
            if (section.size > 0)
                featureSections[feature] = section;
        }
    }

    QHash<PerfHeader::Feature, PerfFileSection>::ConstIterator i;
    for (i = featureSections.begin(); i != featureSections.end(); ++i)
        createFeature(device, stream.byteOrder(), i.value(), i.key());

    return true;
}

QDataStream &operator>>(QDataStream &stream, PerfBuildId &buildId)
{
    quint64 next = 0;
    while (next < buildId.size) {
        PerfBuildId::BuildId build;
        stream >> build.header;
        stream >> build.pid;

        build.id.resize(PerfBuildId::s_idLength);
        stream.readRawData(build.id.data(), PerfBuildId::s_idLength);
        stream.skipRawData(PerfBuildId::s_idPadding);

        uint fileNameLength = build.header.size - sizeof(build.header) - sizeof(build.pid) -
                PerfBuildId::s_idPadding - PerfBuildId::s_idLength;
        if (fileNameLength > static_cast<uint>(std::numeric_limits<int>::max())) {
            qDebug() << "bad file name length";
            return stream;
        }
        build.fileName.resize(fileNameLength);
        stream.readRawData(build.fileName.data(), fileNameLength);
        next += build.header.size;
        buildId.buildIds << build;
        qDebug() << build.header.type << build.id.toHex() << QString::fromLatin1(build.fileName);
    }
    return stream;
}


QDataStream &operator>>(QDataStream &stream, PerfEventHeader &header)
{
    return stream >> header.type >> header.misc >> header.size;
}

QDataStream &operator>>(QDataStream &stream, PerfStringFeature &string)
{
    quint32 length;
    stream >> length;
    if (length > static_cast<quint32>(std::numeric_limits<int>::max())) {
        qDebug() << "bad string length";
        return stream;
    }
    string.value.resize(length);
    stream.readRawData(string.value.data(), length);
    qDebug() << length;
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfNrCpus &nrCpus)
{
    return stream >> nrCpus.online >> nrCpus.available;
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
    qDebug() << numEvents << eventSize;
    PerfStringFeature stringFeature;
    while (numEvents-- > 0) {
        eventDesc.eventDescs << PerfEventDesc::EventDesc();
        PerfEventDesc::EventDesc &currentEvent = eventDesc.eventDescs.last();
        currentEvent.attrs.readFromStream(stream);
        stream >> numIds;
        qDebug() << numIds;
        stream >> stringFeature;
        currentEvent.name = stringFeature.value;
        qDebug() << currentEvent.name;
        while (numIds-- > 0) {
            stream >> id;
            qDebug() << id;
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

    qDebug() << "cores" << numSiblings;
    while (numSiblings-- > 0) {
        stream >> stringFeature;
        cpuTopology.siblingCores << stringFeature.value;
    }

    stream >> numSiblings;
    qDebug() << "threads" << numSiblings;
    while (numSiblings-- > 0) {
        stream >> stringFeature;
        cpuTopology.siblingThreads << stringFeature.value;
    }
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfNumaTopology &numaTopology)
{
    quint32 numNodes;
    stream >> numNodes;
    qDebug() << "numa" << numNodes;

    PerfStringFeature stringFeature;
    while (numNodes-- > 0) {
        PerfNumaTopology::NumaNode node;
        stream >> node.nodeId >> node.memTotal >> node.memFree >> stringFeature;
        node.topology = stringFeature.value;
        qDebug() << node.nodeId << node.memTotal << node.memFree;
        numaTopology.nodes << node;
    }
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfBranchStack &branchStack)
{
    // Doesn't really exist.
    Q_UNUSED(stream);
    Q_UNUSED(branchStack);
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfPmuMappings &pmuMappings)
{
    quint32 numPmus;
    stream >> numPmus;
    qDebug() << "pmus" << numPmus;

    PerfStringFeature stringFeature;
    while (numPmus-- > 0) {
        PerfPmuMappings::Pmu pmu;
        stream >> pmu.type >> stringFeature;
        qDebug() << pmu.type;
        pmu.name = stringFeature.value;
        pmuMappings.pmus << pmu;
    }
    return stream;
}

QDataStream &operator>>(QDataStream &stream, PerfGroupDesc &groupDesc)
{
    quint32 numGroups;
    stream >> numGroups;
    qDebug() << "groups" << numGroups;

    PerfStringFeature stringFeature;
    while (numGroups-- > 0) {
        PerfGroupDesc::GroupDesc desc;
        stream >> stringFeature;
        desc.name = stringFeature.value;
        stream >> desc.leaderIndex >> desc.numMembers;
        groupDesc.groupDescs << desc;
        qDebug() << desc.leaderIndex << desc.numMembers;
    }
    return stream;
}
