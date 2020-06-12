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
#include "perffeatures.h"
#include "perfheader.h"

#include <QIODevice>

enum PerfEventType {

    /*
     * If perf_event_attr.sample_id_all is set then all event types will
     * have the sample_type selected fields related to where/when
     * (identity) an event took place (TID, TIME, ID, STREAM_ID, CPU,
     * IDENTIFIER) described in PERF_RECORD_SAMPLE below, it will be stashed
     * just after the perf_event_header and the fields already present for
     * the existing fields, i.e. at the end of the payload. That way a newer
     * perf.data file will be supported by older perf tools, with these new
     * optional fields being ignored.
     *
     * struct sample_id {
     *    { u32           pid, tid; } && PERF_SAMPLE_TID
     *    { u64           time;     } && PERF_SAMPLE_TIME
     *    { u64           id;       } && PERF_SAMPLE_ID
     *    { u64           stream_id;} && PERF_SAMPLE_STREAM_ID
     *    { u32           cpu, res; } && PERF_SAMPLE_CPU
     *    { u64           id;       } && PERF_SAMPLE_IDENTIFIER
     * } && perf_event_attr::sample_id_all
     *
     * Note that PERF_SAMPLE_IDENTIFIER duplicates PERF_SAMPLE_ID.  The
     * advantage of PERF_SAMPLE_IDENTIFIER is that its position is fixed
     * relative to header.size.
     */

    /*
     * The MMAP events record the PROT_EXEC mappings so that we can
     * correlate userspace IPs to code. They have the following structure:
     *
     * struct {
     *    struct perf_event_header    header;
     *
     *    u32                pid, tid;
     *    u64                addr;
     *    u64                len;
     *    u64                pgoff;
     *    char               filename[];
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_MMAP              = 1,

    /*
     * struct {
     *    struct perf_event_header    header;
     *    u64                id;
     *    u64                lost;
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_LOST              = 2,

    /*
     * struct {
     *    struct perf_event_header    header;
     *
     *    u32                pid, tid;
     *    char               comm[];
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_COMM              = 3,

    /*
     * struct {
     *    struct perf_event_header    header;
     *    u32                pid, ppid;
     *    u32                tid, ptid;
     *    u64                time;
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_EXIT              = 4,

    /*
     * struct {
     *    struct perf_event_header    header;
     *    u64                time;
     *    u64                id;
     *    u64                stream_id;
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_THROTTLE          = 5,
    PERF_RECORD_UNTHROTTLE        = 6,

    /*
     * struct {
     *    struct perf_event_header    header;
     *    u32                pid, ppid;
     *    u32                tid, ptid;
     *    u64                time;
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_FORK              = 7,

    /*
     * struct {
     *    struct perf_event_header    header;
     *    u32                pid, tid;
     *
     *    struct read_format values;
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_READ              = 8,

    /*
     * struct {
     *    struct perf_event_header    header;
     *
     *    #
     *    # Note that PERF_SAMPLE_IDENTIFIER duplicates PERF_SAMPLE_ID.
     *    # The advantage of PERF_SAMPLE_IDENTIFIER is that its position
     *    # is fixed relative to header.
     *    #
     *
     *    { u64            id;        } && PERF_SAMPLE_IDENTIFIER
     *    { u64            ip;        } && PERF_SAMPLE_IP
     *    { u32            pid, tid;  } && PERF_SAMPLE_TID
     *    { u64            time;      } && PERF_SAMPLE_TIME
     *    { u64            addr;      } && PERF_SAMPLE_ADDR
     *    { u64            id;        } && PERF_SAMPLE_ID
     *    { u64            stream_id; } && PERF_SAMPLE_STREAM_ID
     *    { u32            cpu, res;  } && PERF_SAMPLE_CPU
     *    { u64            period;    } && PERF_SAMPLE_PERIOD
     *
     *    { struct read_format values;} && PERF_SAMPLE_READ
     *
     *    { u64            nr,
     *      u64            ips[nr];   } && PERF_SAMPLE_CALLCHAIN
     *
     *    #
     *    # The RAW record below is opaque data wrt the ABI
     *    #
     *    # That is, the ABI doesn't make any promises wrt to
     *    # the stability of its content, it may vary depending
     *    # on event, hardware, kernel version and phase of
     *    # the moon.
     *    #
     *    # In other words, PERF_SAMPLE_RAW contents are not an ABI.
     *    #
     *
     *    { u32            size;
     *      char           data[size];} && PERF_SAMPLE_RAW
     *
     *    { u64                   nr;
     *      { u64 from, to, flags } lbr[nr];} && PERF_SAMPLE_BRANCH_STACK
     *
     *    { u64            abi; # enum perf_sample_regs_abi
     *      u64            regs[weight(mask)]; } && PERF_SAMPLE_REGS_USER
     *
     *    { u64            size;
     *      char           data[size];
     *      u64            dyn_size;    } && PERF_SAMPLE_STACK_USER
     *
     *    { u64            weight;      } && PERF_SAMPLE_WEIGHT
     *    { u64            data_src;    } && PERF_SAMPLE_DATA_SRC
     *    { u64            transaction; } && PERF_SAMPLE_TRANSACTION
     * };
     */
    PERF_RECORD_SAMPLE            = 9,

    /*
     * The MMAP2 records are an augmented version of MMAP, they add
     * maj, min, ino numbers to be used to uniquely identify each mapping
     *
     * struct {
     *    struct perf_event_header    header;
     *
     *    u32                pid, tid;
     *    u64                addr;
     *    u64                len;
     *    u64                pgoff;
     *    u32                maj;
     *    u32                min;
     *    u64                ino;
     *    u64                ino_generation;
     *    u32                prot, flags;
     *    char               filename[];
     *    struct sample_id   sample_id;
     * };
     */
    PERF_RECORD_MMAP2             = 10,

    /*
     * Records a context switch in or out (flagged by
     * PERF_RECORD_MISC_SWITCH_OUT). See also
     * PERF_RECORD_SWITCH_CPU_WIDE.
     *
     * struct {
     *    struct perf_event_header header;
     *    struct sample_id         sample_id;
     * };
     */
    PERF_RECORD_SWITCH            = 14,

    /*
     * CPU-wide version of PERF_RECORD_SWITCH with next_prev_pid and
     * next_prev_tid that are the next (switching out) or previous
     * (switching in) pid/tid.
     *
     * struct {
     *    struct perf_event_header    header;
     *    u32                next_prev_pid;
     *    u32                next_prev_tid;
     *    struct sample_id        sample_id;
     * };
     */
    PERF_RECORD_SWITCH_CPU_WIDE        = 15,

    /*
     * struct {
     *    struct perf_event_header    header;
     *    u32                pid;
     *    u32                tid;
     *    u64                nr_namespaces;
     *    { u64                dev, inode; } [nr_namespaces];
     *    struct sample_id        sample_id;
     * };
     */
    PERF_RECORD_NAMESPACES            = 16,

    /*
     * Record ksymbol register/unregister events:
     *
     * struct {
     *    struct perf_event_header    header;
     *    u64                addr;
     *    u32                len;
     *    u16                ksym_type;
     *    u16                flags;
     *    char                name[];
     *    struct sample_id        sample_id;
     * };
     */
    PERF_RECORD_KSYMBOL            = 17,

    /*
     * Record bpf events:
     *  enum perf_bpf_event_type {
     *    PERF_BPF_EVENT_UNKNOWN        = 0,
     *    PERF_BPF_EVENT_PROG_LOAD    = 1,
     *    PERF_BPF_EVENT_PROG_UNLOAD    = 2,
     *  };
     *
     * struct {
     *    struct perf_event_header    header;
     *    u16                type;
     *    u16                flags;
     *    u32                id;
     *    u8                tag[BPF_TAG_SIZE];
     *    struct sample_id        sample_id;
     * };
     */
    PERF_RECORD_BPF_EVENT            = 18,

    /*
     * struct {
     *    struct perf_event_header    header;
     *    u64                id;
     *    char                path[];
     *    struct sample_id        sample_id;
     * };
     */
    PERF_RECORD_CGROUP              = 19,

    PERF_RECORD_MAX,            /* non-ABI */

    PERF_RECORD_USER_TYPE_START     = 64,
    PERF_RECORD_HEADER_ATTR         = 64,
    PERF_RECORD_HEADER_EVENT_TYPE   = 65, /* depreceated */
    PERF_RECORD_HEADER_TRACING_DATA = 66,
    PERF_RECORD_HEADER_BUILD_ID     = 67,
    PERF_RECORD_FINISHED_ROUND      = 68,
    PERF_RECORD_ID_INDEX            = 69,
    PERF_RECORD_AUXTRACE_INFO       = 70,
    PERF_RECORD_AUXTRACE            = 71,
    PERF_RECORD_AUXTRACE_ERROR      = 72,
    PERF_RECORD_THREAD_MAP          = 73,
    PERF_RECORD_CPU_MAP             = 74,
    PERF_RECORD_STAT_CONFIG         = 75,
    PERF_RECORD_STAT                = 76,
    PERF_RECORD_STAT_ROUND          = 77,
    PERF_RECORD_EVENT_UPDATE        = 78,
    PERF_RECORD_TIME_CONV           = 79,
    PERF_RECORD_HEADER_FEATURE      = 80,
    PERF_RECORD_COMPRESSED          = 81,
    PERF_RECORD_HEADER_MAX
};

enum PERF_RECORD_MISC {
    PERF_RECORD_MISC_SWITCH_OUT = (1 << 13),
};

class PerfRecordSample;

// Use first attribute for deciding if this is present, not the header!
// Why the first?!? idiots ... => encoded in sampleType via sampleIdAll
struct PerfSampleId {
    PerfSampleId(quint64 sampleType = 0, bool sampleIdAll = false) : m_pid(0), m_tid(0), m_time(0),
        m_id(0), m_streamId(0), m_cpu(0), m_res(0),
        m_sampleType(sampleType
                     | (sampleIdAll ? static_cast<quint64>(PerfEventAttributes::SAMPLE_ID_ALL) : 0))
    {}

    qint32 pid() const { return m_pid; }
    qint32 tid() const { return m_tid; }
    quint64 time() const { return m_time; }
    quint64 id() const { return m_id; }
    quint16 fixedLength() const;
    quint64 sampleType() const { return m_sampleType; }
    quint32 cpu() const { return m_cpu; }

private:
    qint32 m_pid;
    qint32 m_tid;
    quint64 m_time;
    quint64 m_id;
    quint64 m_streamId;
    quint32 m_cpu;
    quint32 m_res;

    union {
        quint64 m_ignoredDuplicateId; // In the file format this is the same as id above
        quint64 m_sampleType; // As the id is ignored we can reuse the space for saving the flags
    };

    friend QDataStream &operator>>(QDataStream &stream, PerfSampleId &sampleId);
    friend QDataStream &operator>>(QDataStream &stream, PerfRecordSample &record);
};

QDataStream &operator>>(QDataStream &stream, PerfSampleId &sampleId);

class PerfRecord {
public:
    qint32 pid() const { return m_sampleId.pid(); }
    qint32 tid() const { return m_sampleId.tid(); }
    quint64 time() const { return m_sampleId.time(); }
    quint64 id() const { return m_sampleId.id(); }
    quint32 cpu() const { return m_sampleId.cpu(); }
    quint16 size() const { return m_header.size; }
    quint16 misc() const { return m_header.misc; }

protected:
    PerfRecord(const PerfEventHeader *header, quint64 sampleType, bool sampleIdAll);
    PerfEventHeader m_header;
    PerfSampleId m_sampleId;

    quint16 fixedLength() const { return m_header.fixedLength() + m_sampleId.fixedLength(); }
};

class PerfRecordMmap2;
class PerfRecordMmap : public PerfRecord {
public:
    PerfRecordMmap(PerfEventHeader *header = nullptr, quint64 sampleType = 0,
                   bool sampleIdAll = false);

    // The pids and tids in the sampleId are always 0 in this case. Go figure ...
    qint32 pid() const { return m_pid; }
    qint32 tid() const { return m_tid; }

    quint64 addr() const { return m_addr; }
    quint64 len() const { return m_len; }
    quint64 pgoff() const { return m_pgoff; }
    const QByteArray &filename() const { return m_filename; }

protected:
    QDataStream &readNumbers(QDataStream &stream);
    QDataStream &readFilename(QDataStream &stream, quint16 filenameLength);
    QDataStream &readSampleId(QDataStream &stream);
    quint16 fixedLength() const;

private:
    qint32 m_pid;
    qint32 m_tid;
    quint64 m_addr;
    quint64 m_len;
    quint64 m_pgoff;
    QByteArray m_filename;

    friend QDataStream &operator>>(QDataStream &stream, PerfRecordMmap &record);
    friend QDataStream &operator>>(QDataStream &stream, PerfRecordMmap2 &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordMmap &record);

class PerfRecordMmap2 : public PerfRecordMmap
{
public:
    PerfRecordMmap2(PerfEventHeader *header = nullptr, quint64 sampleType = 0,
                    bool sampleIdAll = false);

    quint32 prot() const { return m_prot; }

protected:
    QDataStream &readNumbers(QDataStream &stream);

private:
    quint32 m_maj;
    quint32 m_min;
    quint64 m_ino;
    quint64 m_ino_generation;
    quint32 m_prot;
    quint32 m_flags;

    quint16 fixedLength() const;

    friend QDataStream &operator>>(QDataStream &stream, PerfRecordMmap2 &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordMmap2 &record);

class PerfRecordLost : public PerfRecord {
public:
    PerfRecordLost(PerfEventHeader *header = nullptr, quint64 sampleType = 0,
                   bool sampleIdAll = false);
private:
    quint64 m_id;
    quint64 m_lost;

    friend QDataStream &operator>>(QDataStream &stream, PerfRecordLost &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordLost &record);

class PerfRecordComm : public PerfRecord {
public:
    PerfRecordComm(PerfEventHeader *header = nullptr, quint64 sampleType = 0,
                   bool sampleIdAll = false);
    const QByteArray &comm() const { return m_comm; }

    // The pids and tids in the sampleId are always 0 in this case. Go figure ...
    qint32 pid() const { return m_pid; }
    qint32 tid() const { return m_tid; }

private:
    qint32 m_pid;
    qint32 m_tid;
    QByteArray m_comm;

    quint16 fixedLength() const { return PerfRecord::fixedLength() + sizeof(m_pid) + sizeof(m_tid); }

    friend QDataStream &operator>>(QDataStream &stream, PerfRecordComm &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordComm &record);

class PerfRecordSample : public PerfRecord {
public:
    PerfRecordSample(const PerfEventHeader *header = nullptr,
                     const PerfEventAttributes *attributes = nullptr);
    quint64 registerAbi() const { return m_registerAbi; }
    quint64 registerValue(int reg) const;
    quint64 ip() const { return m_ip; }
    const QByteArray &userStack() const { return m_userStack; }
    const QList<quint64> &callchain() const { return m_callchain; }
    quint64 period() const { return m_period; }
    quint64 weight() const { return m_weight; }
    const QByteArray &rawData() const { return m_rawData; }

    struct ReadFormat {
        quint64 value;
        quint64 id;
    };

    QList<ReadFormat> readFormats() const { return m_readFormats; }

    struct BranchFlags {
        quint64 mispred: 1;
        quint64 predicted: 1;
        quint64 in_tx: 1;
        quint64 abort: 1;
        quint64 cycles: 16;
        quint64 type: 4;
        quint64 reserved: 40;
    };

    struct BranchEntry {
        quint64 from;
        quint64 to;
        BranchFlags flags;
    };
    const QList<BranchEntry> &branchStack() const { return m_branchStack; }

private:

    quint64 m_readFormat;
    quint64 m_registerMask;

    quint64 m_ip;
    quint64 m_addr;
    quint64 m_period;
    quint64 m_timeEnabled;
    quint64 m_timeRunning;

    quint64 m_registerAbi;
    quint64 m_weight;
    quint64 m_dataSrc;
    quint64 m_transaction;

    QList<ReadFormat> m_readFormats;
    QList<quint64> m_callchain;
    QByteArray m_rawData;
    QList<BranchEntry> m_branchStack;
    QList<quint64> m_registers;
    QByteArray m_userStack;

    friend QDataStream &operator>>(QDataStream &stream, PerfRecordSample &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordSample &record);

class PerfRecordAttr : public PerfRecord
{
public:
    PerfRecordAttr(const PerfEventHeader *header = nullptr, quint64 sampleType = 0,
                   bool sampleIdAll = false);

    PerfRecordAttr(const PerfEventAttributes &attributes, const QList<quint64> &ids);

    const PerfEventAttributes &attr() const { return m_attr; }
    const QList<quint64> &ids() const { return m_ids; }

private:
    PerfEventAttributes m_attr;
    QList<quint64> m_ids;

    friend QDataStream &operator>>(QDataStream &stream, PerfRecordAttr &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordAttr &record);

class PerfRecordFork : public PerfRecord
{
public:
    PerfRecordFork(PerfEventHeader *header = nullptr, quint64 sampleType = 0,
                   bool sampleIdAll = false);
    qint32 childTid() const { return m_tid; }
    qint32 childPid() const { return m_pid; }
private:
    qint32 m_pid, m_ppid;
    qint32 m_tid, m_ptid;
    quint64 m_time;

    friend QDataStream &operator>>(QDataStream &stream, PerfRecordFork &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordFork &record);

typedef PerfRecordFork PerfRecordExit;

class PerfRecordContextSwitch : public PerfRecord
{
public:
    PerfRecordContextSwitch(PerfEventHeader *header = 0, quint64 sampleType = 0,
                            bool sampleIdAll = false);

private:
    friend QDataStream &operator>>(QDataStream &stream, PerfRecordContextSwitch &record);
};

QDataStream &operator>>(QDataStream &stream, PerfRecordContextSwitch &record);

class PerfUnwind;
class PerfData : public QObject
{
    Q_OBJECT
public:
    PerfData(PerfUnwind *destination, const PerfHeader *header, PerfAttributes *attributes);
    void setSource(QIODevice *source);

public slots:
    void read();
    void finishReading();

signals:
    void finished();
    void error();

private:

    enum ReadStatus {
        Rerun,
        SignalError,
        SignalFinished
    };

    QIODevice *m_source;
    PerfUnwind *m_destination;

    const PerfHeader *m_header;
    PerfAttributes *m_attributes;
    PerfEventHeader m_eventHeader;
    PerfTracingData m_tracingData;

    ReadStatus processEvents(QDataStream &stream);
    ReadStatus doRead();
};
