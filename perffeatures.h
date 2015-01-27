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

#ifndef PERFFEATURE_H
#define PERFFEATURE_H

#include "perfheader.h"
#include "perfattributes.h"

#include <QHash>
#include <QVector>

struct PerfEventHeader {
    PerfEventHeader() : type(0), misc(0), size(0) {}
    quint32 type;
    quint16 misc;
    quint16 size;
};

QDataStream &operator>>(QDataStream &stream, PerfEventHeader &header);

struct PerfBuildId {
    PerfBuildId() : size(0) {}

    static const uint s_idLength = 20;
    static const uint s_idPadding = 4; // 20 aligned to 8 gives 24 => 4 unused bytes
    static const uint s_pathMax = 4096;

    struct BuildId {
        PerfEventHeader header;
        quint32 pid;
        QByteArray id;
        QByteArray fileName;
    };

    quint64 size;
    QList<BuildId> buildIds;
};

QDataStream &operator>>(QDataStream &stream, PerfBuildId &buildId);

struct PerfStringFeature {
    QByteArray value;
};

QDataStream &operator>>(QDataStream &stream, PerfStringFeature &stringFeature);

struct PerfNrCpus {
    quint32 online;
    quint32 available;
};

QDataStream &operator>>(QDataStream &stream, PerfNrCpus &numCpus);

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

struct PerfBranchStack {
};

QDataStream &operator>>(QDataStream &stream, PerfBranchStack &branchStack);

struct PerfPmuMappings {

    struct Pmu {
        quint32 type;
        QByteArray name;
    };
    QList<Pmu> pmus;
};

QDataStream &operator>>(QDataStream &stream, PerfPmuMappings &pmuMappings);

struct PerfGroupDesc {

    struct GroupDesc {
		QByteArray name;
		quint32 leaderIndex;
		quint32 numMembers;
	};

    QList<GroupDesc> groupDescs;
};

QDataStream &operator>>(QDataStream &stream, PerfGroupDesc &groupDesc);

class PerfFeatures
{
public:
    PerfFeatures();
    ~PerfFeatures();

    bool read(QIODevice *device, const PerfHeader *header);
    const QByteArray &architecture() const { return m_arch.value; }

private:
    void createFeature(QIODevice *device, QDataStream::ByteOrder byteOrder,
                       const PerfFileSection &section, PerfHeader::Feature featureId);

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
    PerfBranchStack m_branchStack;
    PerfPmuMappings m_pmuMappings;
    PerfGroupDesc m_groupDesc;
};

#endif // PERFFEATURE_H
