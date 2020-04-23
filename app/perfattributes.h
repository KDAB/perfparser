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
#include "perfheader.h"

#include <QDataStream>
#include <QHash>
#include <QIODevice>

class PerfEventAttributes {
public:
    enum Sizes {
        SIZE_VER0 =  64, /* sizeof first published struct */
        SIZE_VER1 =  72, /* add: config2 */
        SIZE_VER2 =  80, /* add: branch_sample_type */
        SIZE_VER3 =  96, /* add: sample_regs_user, sample_stack_user */
        SIZE_VER4 = 104, /* add: sample_regs_intr */
        SIZE_VER5 = 112, /* add: aux_watermark */
    };

    PerfEventAttributes();

    bool sampleIdAll() const { return m_sampleIdAll; }
    quint64 sampleType() const { return m_sampleType; }
    quint64 readFormat() const { return m_readFormat; }
    quint64 sampleRegsUser() const { return m_sampleRegsUser; }
    quint32 size() const { return m_size; }
    quint32 type() const { return m_type; }
    quint64 config() const { return m_config; }
    int sampleIdOffset() const;
    bool usesFrequency() const { return m_freq; };
    quint64 frequenyOrPeriod() const { return m_sampleFreq; }

    QByteArray name() const;

    enum ReadFormat {
        FORMAT_TOTAL_TIME_ENABLED = 1U << 0,
        FORMAT_TOTAL_TIME_RUNNING = 1U << 1,
        FORMAT_ID                 = 1U << 2,
        FORMAT_GROUP              = 1U << 3,

        FORMAT_MAX                = 1U << 4
    };

    /*
     * Bits that can be set in sampleType to request information
     * in the overflow packets.
     */
    enum SampleFormat {
        SAMPLE_IP           = 1U << 0,
        SAMPLE_TID          = 1U << 1,
        SAMPLE_TIME         = 1U << 2,
        SAMPLE_ADDR         = 1U << 3,
        SAMPLE_READ         = 1U << 4,
        SAMPLE_CALLCHAIN    = 1U << 5,
        SAMPLE_ID           = 1U << 6,
        SAMPLE_CPU          = 1U << 7,
        SAMPLE_PERIOD       = 1U << 8,
        SAMPLE_STREAM_ID    = 1U << 9,
        SAMPLE_RAW          = 1U << 10,
        SAMPLE_BRANCH_STACK = 1U << 11,
        SAMPLE_REGS_USER    = 1U << 12,
        SAMPLE_STACK_USER   = 1U << 13,
        SAMPLE_WEIGHT       = 1U << 14,
        SAMPLE_DATA_SRC     = 1U << 15,
        SAMPLE_IDENTIFIER   = 1U << 16,
        SAMPLE_TRANSACTION  = 1U << 17,

        SAMPLE_MAX          = 1U << 18,
        SAMPLE_ID_ALL       = 1U << 31 // extra flag, to check if the sample has a sample ID at all
    };

    /*
     * attr.type()
     */
    enum TypeId {
        TYPE_HARDWARE       = 0,
        TYPE_SOFTWARE       = 1,
        TYPE_TRACEPOINT     = 2,
        TYPE_HARDWARE_CACHE = 3,
        TYPE_RAW            = 4,
        TYPE_BREAKPOINT     = 5,
        TYPE_MAX,           /* non-ABI */
    };

    /*
     * Generalized performance event eventId types, used by the
     * attr.event_id parameter of the sys_perf_event_open()
     * syscall.
     *
     * Ends up in attr.config() if type() is TYPE_HARDWARE
     */
    enum HardwareId {
        /*
         * Common hardware events, generalized by the kernel:
         */
        HARDWARE_CPU_CYCLES              = 0,
        HARDWARE_INSTRUCTIONS            = 1,
        HARDWARE_CACHE_REFERENCES        = 2,
        HARDWARE_CACHE_MISSES            = 3,
        HARDWARE_BRANCH_INSTRUCTIONS     = 4,
        HARDWARE_BRANCH_MISSES           = 5,
        HARDWARE_BUS_CYCLES              = 6,
        HARDWARE_STALLED_CYCLES_FRONTEND = 7,
        HARDWARE_STALLED_CYCLES_BACKEND  = 8,
        HARDWARE_REF_CPU_CYCLES          = 9,
        HARDWARE_MAX,                    /* non-ABI */
    };

    /*
     * Generalized hardware cache events:
     *
     * attr.config() for type() == TYPE_HW_CACHE.
     *
     * Encoding is (cacheId | (cacheOpId << 8) | (cacheOpResultId << 16))
     * for example -e L1-dcache-store-misses results in config == 0x10100, or
     * -e LLC-loads in config == 0x000002.
     */
    enum HardwareCacheId {
        HARDWARE_CACHE_L1D  = 0,
        HARDWARE_CACHE_L1I  = 1,
        HARDWARE_CACHE_LL   = 2,
        HARDWARE_CACHE_DTLB = 3,
        HARDWARE_CACHE_ITLB = 4,
        HARDWARE_CACHE_BPU  = 5,
        HARDWARE_CACHE_NODE = 6,

        HARDWARE_CACHE_MAX, /* non-ABI */
    };

    enum HardwareCacheOperationId {
        HARDWARE_CACHE_OPERATION_READ     = 0,
        HARDWARE_CACHE_OPERATION_WRITE    = 1,
        HARDWARE_CACHE_OPERATION_PREFETCH = 2,
        HARDWARE_CACHE_OPERATION_MAX,     /* non-ABI */
    };

    enum HardwareCacheOperationResultId {
        HARDWARE_CACHE_RESULT_OPERATION_ACCESS = 0,
        HARDWARE_CACHE_RESULT_OPERATION_MISS   = 1,
        HARDWARE_CACHE_RESULT_OPERATION_MAX,   /* non-ABI */
    };

    /*
     * Special "software" events provided by the kernel, even if the hardware
     * does not support performance events. These events measure various
     * physical and sw events of the kernel (and allow the profiling of them as
     * well):
     */
    enum SoftwareId {
        SOFTWARE_CPU_CLOCK        = 0,
        SOFTWARE_TASK_CLOCK       = 1,
        SOFTWARE_PAGE_FAULTS      = 2,
        SOFTWARE_CONTEXT_SWITCHES = 3,
        SOFTWARE_CPU_MIGRATIONS   = 4,
        SOFTWARE_PAGE_FAULTS_MIN  = 5,
        SOFTWARE_PAGE_FAULTS_MAJ  = 6,
        SOFTWARE_ALIGNMENT_FAULTS = 7,
        SOFTWARE_EMULATION_FAULTS = 8,
        SOFTWARE_DUMMY            = 9,
        SOFTWARE_MAX,             /* non-ABI */
    };

    bool operator==(const PerfEventAttributes &rhs) const;

private:

    /*
     * Major type: hardware/software/tracepoint/etc.
     */
    quint32 m_type;

    /*
     * Size of the attr structure, for fwd/bwd compat.
     */
    quint32 m_size;

    /*
     * Type specific configuration information.
     */
    quint64 m_config;

    union {
        quint64 m_samplePeriod;
        quint64 m_sampleFreq;
    };

    quint64 m_sampleType;
    quint64 m_readFormat;

    quint64 m_disabled      : 1, /* off by default         */
            m_inherit       : 1, /* children inherit it    */
            m_pinned        : 1, /* must always be on PMU  */
            m_exclusive     : 1, /* only group on PMU      */
            m_excludeUser   : 1, /* don't count user       */
            m_excludeKernel : 1, /* ditto kernel           */
            m_excludeHv     : 1, /* ditto hypervisor       */
            m_excludeIdle   : 1, /* don't count when idle  */
            m_mmap          : 1, /* include mmap data      */
            m_comm          : 1, /* include comm data      */
            m_freq          : 1, /* use freq, not period   */
            m_inheritStat   : 1, /* per task counts        */
            m_enableOnExec  : 1, /* next exec enables      */
            m_task          : 1, /* trace fork/exit        */
            m_watermark     : 1, /* wakeup_watermark       */
            /*
             * m_preciseIp:
             *
             *  0 - SAMPLE_IP can have arbitrary skid
             *  1 - SAMPLE_IP must have constant skid
             *  2 - SAMPLE_IP requested to have 0 skid
             *  3 - SAMPLE_IP must have 0 skid
             *
             *  See also PERF_RECORD_MISC_EXACT_IP
             */
            m_preciseIp     : 2, /* skid constraint        */
            m_mmapData      : 1, /* non-exec mmap data     */
            m_sampleIdAll   : 1, /* sample_type all events */

            m_excludeHost   : 1, /* don't count in host    */
            m_excludeGuest  : 1, /* don't count in guest   */

            /* m_excludeCallchainKernel */ : 1, /* exclude kernel callchains */
            /* m_excludeCallchainUser */ : 1, /* exclude user callchains   */

            m_reserved1     : 41;

    union {
        quint32 m_wakeupEvents;    /* wakeup every n events */
        quint32 m_wakeupWatermark; /* bytes before wakeup   */
    };

    quint32 m_bpType;
    union {
        quint64 m_bpAddr;
        quint64 m_config1; /* extension of config */
    };

    union {
        quint64 m_bpLen;
        quint64 m_config2; /* extension of config1 */
    };

    quint64 m_branchSampleType; /* enum perf_branch_sample_type */

    /*
     * Defines set of user regs to dump on samples.
     * See asm/perf_regs.h for details.
     */
    quint64 m_sampleRegsUser;

    /*
     * Defines size of the user stack to dump on samples.
     */
    quint32 m_sampleStackUser;

    qint32  m_clockid;

    /*
     * Defines set of regs to dump for each sample
     * state captured on:
     *  - precise = 0: PMU interrupt
     *  - precise > 0: sampled instruction
     *
     * See asm/perf_regs.h for details.
     */
    quint64 m_sampleRegsIntr;

    /*
     * Wakeup watermark for AUX area
     */
    quint32 m_auxWatermark;
    quint16 m_sampleMaxStack;

    /* Align to u64. */
    quint16 m_reserved2;

    friend QDataStream &operator>>(QDataStream &stream, PerfEventAttributes &attrs);
};

QDataStream &operator>>(QDataStream &stream, PerfEventAttributes &attrs);

class PerfAttributes {
public:
    bool read(QIODevice *device, PerfHeader *header);
    void addAttributes(quint64 id, const PerfEventAttributes &attributes);
    void setGlobalAttributes(const PerfEventAttributes &attributes);

    const QHash<quint64, PerfEventAttributes> &attributes() const { return m_attributes; }
    const PerfEventAttributes &attributes(quint64 id) const;
    const PerfEventAttributes &globalAttributes() const { return m_globalAttributes; }

private:
    PerfEventAttributes m_globalAttributes;
    QHash<quint64, PerfEventAttributes> m_attributes;
};
