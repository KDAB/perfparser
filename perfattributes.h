#ifndef PERFATTRIBUTES_H
#define PERFATTRIBUTES_H

#include <QIODevice>
#include <QDataStream>
#include <QHash>
#include "perffilesection.h"
#include "perfheader.h"

class PerfEventAttributes {
public:
    PerfEventAttributes();

    bool readFromStream(QDataStream &stream);

    bool sampleIdAll() const { return m_sampleIdAll; }
    quint64 sampleType() const { return m_sampleType; }
    quint64 readFormat() const { return m_readFormat; }
    quint64 sampleRegsUser() const { return m_sampleRegsUser; }
    int sampleIdOffset() const;

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
        SAMPLE_BRANCH_STACK	= 1U << 11,
        SAMPLE_REGS_USER    = 1U << 12,
        SAMPLE_STACK_USER   = 1U << 13,
        SAMPLE_WEIGHT       = 1U << 14,
        SAMPLE_DATA_SRC     = 1U << 15,
        SAMPLE_IDENTIFIER   = 1U << 16,
        SAMPLE_TRANSACTION  = 1U << 17,

        SAMPLE_MAX          = 1U << 18
    };

private:

    /*
     * Major type: hardware/software/tracepoint/etc.
     */
    quint32 m_type;

    /*
     * Size of the attr structure, for fwd/bwd compat.
     */
    quint32	m_size;

    /*
     * Type specific configuration information.
     */
    quint64	m_config;

    union {
        quint64	m_samplePeriod;
        quint64	m_sampleFreq;
    };

    quint64	m_sampleType;
    quint64	m_readFormat;

    quint64	m_disabled      : 1, /* off by default         */
            m_inherit       : 1, /* children inherit it    */
            m_pinned        : 1, /* must always be on PMU  */
            m_exclusive     : 1, /* only group on PMU      */
            m_excludeUser   : 1, /* don't count user       */
            m_excludeKernel : 1, /* ditto kernel           */
            m_excludeHv     : 1, /* ditto hypervisor       */
            m_excludeIdle   : 1, /* don't count when idle  */
            m_mmap          : 1, /* include mmap data      */
            m_comm	        : 1, /* include comm data      */
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

            m_reserved1     : 43;

    union {
        quint32	m_wakeupEvents;	  /* wakeup every n events */
        quint32	m_wakeupWatermark; /* bytes before wakeup   */
    };

    quint32	m_bpType;
    union {
        quint64	m_bpAddr;
        quint64	m_config1; /* extension of config */
    };

    union {
        quint64	m_bpLen;
        quint64	m_config2; /* extension of config1 */
    };

    quint64	m_branchSampleType; /* enum perf_branch_sample_type */

	/*
	 * Defines set of user regs to dump on samples.
	 * See asm/perf_regs.h for details.
	 */
	quint64	m_sampleRegsUser;

	/*
	 * Defines size of the user stack to dump on samples.
	 */
	quint32	m_sampleStackUser;

	/* Align to u64. */
	quint32	m_reserved2;
};


class PerfAttributes {
public:
    bool read(QIODevice *device, PerfHeader *header);

    const PerfEventAttributes &attributes(quint64 id) const;
    const PerfEventAttributes &globalAttributes() const { return m_globalAttributes; }

private:
    PerfEventAttributes m_globalAttributes;
    QHash<quint64, PerfEventAttributes> m_attributes;
};

#endif // PERFATTRIBUTES_H
