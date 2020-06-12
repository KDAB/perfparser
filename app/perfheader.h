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

#include "perffilesection.h"

#include <QDataStream>
#include <QIODevice>

class PerfHeader : public QObject {
    Q_OBJECT
public:
    PerfHeader(QIODevice *source);

    enum Feature {
        RESERVED       = 0, /* always cleared */
        FIRST_FEATURE  = 1,
        TRACING_DATA   = 1,
        BUILD_ID,

        HOSTNAME,
        OSRELEASE,
        VERSION,
        ARCH,
        NRCPUS,
        CPUDESC,
        CPUID,
        TOTAL_MEM,
        CMDLINE,
        EVENT_DESC,
        CPU_TOPOLOGY,
        NUMA_TOPOLOGY,
        BRANCH_STACK,
        PMU_MAPPINGS,
        GROUP_DESC,
        AUXTRACE,
        STAT,
        CACHE,
        SAMPLE_TIME,
        MEM_TOPOLOGY,
        CLOCKID,
        DIR_FORMAT,
        BPF_PROG_INFO,
        BPF_BTF,
        COMPRESSED,
        CPU_PMU_CAPS,
        LAST_FEATURE,
        FEAT_BITS      = 256,
    };
    Q_ENUM(Feature)

    QDataStream::ByteOrder byteOrder() const;

    bool hasFeature(Feature feature) const;
    void setFeature(Feature feature);
    void clearFeature(Feature feature);

    qint64 numAttrs() const { return m_attrs.size > 0 ? m_attrs.size / m_attrSize : 0ll; }
    qint64 attrSize() const { return m_attrSize; }
    const PerfFileSection &attrs() const { return m_attrs; }

    qint64 featureOffset() const { return m_data.offset + m_data.size; }
    qint64 dataOffset() const { return m_data.offset; }
    qint64 dataSize() const { return m_data.size; }
    bool isPipe() const { return m_size == pipeHeaderFixedLength(); }

    qint64 size() const { return m_size; }

public slots:
    void read();

signals:
    void finished();
    void error();

private:
    static quint16 fileHeaderFixedLength();
    static quint16 pipeHeaderFixedLength();

    QIODevice *m_source;

    qint64 m_magic;
    qint64 m_size;
    qint64 m_attrSize;

    PerfFileSection m_attrs;
    PerfFileSection m_data;
    PerfFileSection m_eventTypes;

    quint64 m_features[FEAT_BITS / 64 + ((FEAT_BITS % 64) > 0 ? 1 : 0)];

    static const qint64 s_magicSame = 0x32454c4946524550LL;
    static const qint64 s_magicSwitched = 0x50455246494c4532LL;
};
