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

#pragma once

#include "perfattributes.h"
#include "perfheader.h"
#include "perftracingdata.h"

#include <QHash>
#include <QVector>

struct PerfEventHeader {
    PerfEventHeader() : type(0), misc(0), size(0) {}
    quint32 type;
    quint16 misc;
    quint16 size;

    static quint16 fixedLength() { return sizeof(type) + sizeof(misc) + sizeof(size); }
};

QDataStream &operator>>(QDataStream &stream, PerfEventHeader &header);

struct PerfBuildId {
    PerfBuildId() : size(0) {}

    static const quint16 s_idLength = 20;
    static const quint16 s_idPadding = 4; // 20 aligned to 8 gives 24 => 4 unused bytes
    static const quint16 s_pathMax = 4096;

    struct BuildId {
        qint32 pid;
        QByteArray id; // raw id, use .toHex() to get something human-readable
        QByteArray fileName;
    };

    qint64 size;
    QList<BuildId> buildIds;
};

QDataStream &operator>>(QDataStream &stream, PerfBuildId &buildId);
QDataStream &operator<<(QDataStream &stream, const PerfBuildId::BuildId &buildId);

struct PerfStringFeature {
    QByteArray value;
};

QDataStream &operator>>(QDataStream &stream, PerfStringFeature &stringFeature);

struct PerfNrCpus {
    quint32 online;
    quint32 available;
};

QDataStream &operator>>(QDataStream &stream, PerfNrCpus &numCpus);
QDataStream &operator<<(QDataStream &stream, const PerfNrCpus &numCpus);

struct PerfTotalMem {
    quint64 totalMem;
};

QDataStream &operator>>(QDataStream &stream, PerfTotalMem &totalMem);

struct PerfCmdline {
    QList<QByteArray> cmdline;
};

QDataStream &operator>>(QDataStream &stream, PerfCmdline &cmdline);

struct PerfEventDesc {
    struct EventDesc {
        PerfEventAttributes attrs;
        QByteArray name;
        QList<quint64> ids;
    };

    QList<EventDesc> eventDescs;
};

QDataStream &operator>>(QDataStream &stream, PerfEventDesc &eventDesc);

struct PerfCpuTopology {

    // Some kind of bitmask. Not so important for now
    QList<QByteArray> siblingCores;
    QList<QByteArray> siblingThreads;
};

QDataStream &operator>>(QDataStream &stream, PerfCpuTopology &cpuTopology);
QDataStream &operator<<(QDataStream &stream, const PerfCpuTopology &cpuTopology);

struct PerfNumaTopology {

    struct NumaNode {
        quint32 nodeId;
        quint64 memTotal;
        quint64 memFree;
        QByteArray topology;
    };

    QList<NumaNode> nodes;
};

QDataStream &operator>>(QDataStream &stream, PerfNumaTopology &numaTopology);
QDataStream &operator<<(QDataStream &stream, const PerfNumaTopology::NumaNode &numaNode);

struct PerfPmuMappings {

    struct Pmu {
        quint32 type;
        QByteArray name;
    };
    QList<Pmu> pmus;
};

QDataStream &operator>>(QDataStream &stream, PerfPmuMappings &pmuMappings);
QDataStream &operator<<(QDataStream &stream, const PerfPmuMappings::Pmu &pmu);

struct PerfGroupDesc {

    struct GroupDesc {
        QByteArray name;
        quint32 leaderIndex;
        quint32 numMembers;
    };

    QList<GroupDesc> groupDescs;
};

QDataStream &operator>>(QDataStream &stream, PerfGroupDesc &groupDesc);
QDataStream &operator<<(QDataStream &stream, const PerfGroupDesc::GroupDesc &groupDesc);

class PerfFeatures
{
public:
    PerfFeatures();
    ~PerfFeatures();

    bool read(QIODevice *device, const PerfHeader *header);
    const QByteArray &architecture() const { return m_arch.value; }
    void setArchitecture(const QByteArray &arch) { m_arch.value = arch; }

    PerfTracingData tracingData() const { return m_tracingData; }
    QList<PerfBuildId::BuildId> buildIds() const { return m_buildId.buildIds; }
    QByteArray hostName() const { return m_hostName.value; }
    QByteArray osRelease() const { return m_osRelease.value; }
    QByteArray version() const { return m_version.value; }
    PerfNrCpus nrCpus() const { return m_nrCpus; }
    QByteArray cpuDesc() const { return m_cpuDesc.value; }
    QByteArray cpuId() const { return m_cpuId.value; }
    quint64 totalMem() const { return m_totalMem.totalMem; }
    QList<QByteArray> cmdline() const { return m_cmdline.cmdline; }
    PerfEventDesc eventDesc() const { return m_eventDesc; }
    PerfCpuTopology cpuTopology() const { return m_cpuTopology; }
    QList<PerfNumaTopology::NumaNode> numaTopology() const { return m_numaToplogy.nodes; }
    QList<PerfPmuMappings::Pmu> pmuMappings() const { return m_pmuMappings.pmus; }
    QList<PerfGroupDesc::GroupDesc> groupDescs() const { return m_groupDesc.groupDescs; }

private:
    void createFeature(QIODevice *device, QDataStream::ByteOrder byteOrder,
                       const PerfFileSection &section, PerfHeader::Feature featureId);

    PerfTracingData m_tracingData;
    PerfBuildId m_buildId;
    PerfStringFeature m_hostName;
    PerfStringFeature m_osRelease;
    PerfStringFeature m_version;
    PerfStringFeature m_arch;
    PerfNrCpus m_nrCpus;
    PerfStringFeature m_cpuDesc;
    PerfStringFeature m_cpuId;
    PerfTotalMem m_totalMem;
    PerfCmdline m_cmdline;
    PerfEventDesc m_eventDesc;
    PerfCpuTopology m_cpuTopology;
    PerfNumaTopology m_numaToplogy;
    PerfPmuMappings m_pmuMappings;
    PerfGroupDesc m_groupDesc;
};
