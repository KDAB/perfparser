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

#include "perfdata.h"
#include "perfkallsyms.h"
#include "perfregisterinfo.h"
#include "perftracingdata.h"
#include "perfaddresscache.h"

#include <libdwfl.h>

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QIODevice>
#include <QList>
#include <QObject>
#include <QString>
#include <QMap>

#include <limits>

class PerfSymbolTable;
class PerfUnwind : public QObject
{
    Q_OBJECT
public:
    enum EventType {
        ThreadStart,
        ThreadEnd,
        Command,
        LocationDefinition,
        SymbolDefinition,
        StringDefinition,
        LostDefinition,
        FeaturesDefinition,
        Error,
        Progress,
        TracePointFormat,
        AttributesDefinition,
        ContextSwitchDefinition,
        Sample,
        TracePointSample,
        InvalidType
    };

    struct Location {
        explicit Location(quint64 address = 0, qint32 file = -1,
                          quint32 pid = 0, qint32 line = 0, qint32 column = 0,
                          qint32 parentLocationId = -1) :
            address(address), file(file), pid(pid), line(line), column(column),
            parentLocationId(parentLocationId) {}

        quint64 address;
        qint32 file;
        quint32 pid;
        qint32 line;
        qint32 column;
        qint32 parentLocationId;
    };

    struct Symbol {
        explicit Symbol(qint32 name = -1, qint32 binary = -1, qint32 path = -1,
                        bool isKernel = false) :
            name(name), binary(binary), path(path), isKernel(isKernel)
        {}

        qint32 name;
        qint32 binary;
        qint32 path;
        bool isKernel;
    };

    struct UnwindInfo {
        UnwindInfo() : frames(0), unwind(nullptr), sample(nullptr), maxFrames(64),
            firstGuessedFrame(-1), isInterworking(false) {}

        QHash<qint32, QHash<quint64, Dwarf_Word>> stackValues;
        QVector<qint32> frames;
        PerfUnwind *unwind;
        const PerfRecordSample *sample;
        int maxFrames;
        int firstGuessedFrame;
        bool isInterworking;
    };

    struct Stats
    {
        Stats()
            : numSamples(0), numMmaps(0), numRounds(0), numBufferFlushes(0),
            numTimeViolatingSamples(0), numTimeViolatingMmaps(0),
            numSamplesInRound(0), numMmapsInRound(0), numTaskEventsInRound(0),
            maxSamplesPerRound(0), maxMmapsPerRound(0), maxTaskEventsPerRound(0),
            maxSamplesPerFlush(0), maxMmapsPerFlush(0), maxTaskEventsPerFlush(0),
            maxBufferSize(0), maxTotalEventSizePerRound(0),
            maxTime(0), maxTimeBetweenRounds(0), maxReorderTime(0),
            lastRoundTime(0), totalEventSizePerRound(0),
            enabled(false)
        {}

        void addEventTime(quint64 time);
        void finishedRound();

        quint64 numSamples;
        quint64 numMmaps;
        quint64 numRounds;
        quint64 numBufferFlushes;
        quint64 numTimeViolatingSamples;
        quint64 numTimeViolatingMmaps;
        uint numSamplesInRound;
        uint numMmapsInRound;
        uint numTaskEventsInRound;
        uint maxSamplesPerRound;
        uint maxMmapsPerRound;
        uint maxTaskEventsPerRound;
        uint maxSamplesPerFlush;
        uint maxMmapsPerFlush;
        uint maxTaskEventsPerFlush;
        uint maxBufferSize;
        uint maxTotalEventSizePerRound;
        quint64 maxTime;
        quint64 maxTimeBetweenRounds;
        quint64 maxReorderTime;
        quint64 lastRoundTime;
        uint totalEventSizePerRound;
        bool enabled;
    };

    static const qint32 s_kernelPid;
    static QString defaultDebugInfoPath();
    static QString defaultKallsymsPath();

    PerfUnwind(QIODevice *output, const QString &systemRoot = QDir::rootPath(),
               const QString &debugPath = defaultDebugInfoPath(),
               const QString &extraLibs = QString(), const QString &appPath = QString(),
               bool printStats = false);
    ~PerfUnwind();

    QString kallsymsPath() const { return m_kallsymsPath; }
    void setKallsymsPath(const QString &kallsymsPath) { m_kallsymsPath = kallsymsPath; }

    bool ignoreKallsymsBuildId() const { return m_ignoreKallsymsBuildId; }
    void setIgnoreKallsymsBuildId(bool ignore) { m_ignoreKallsymsBuildId = ignore; }

    uint maxEventBufferSize() const { return m_maxEventBufferSize; }
    void setMaxEventBufferSize(uint size);

    uint targetEventBufferSize() const { return m_targetEventBufferSize; }
    void setTargetEventBufferSize(uint size);

    int maxUnwindFrames() const { return m_currentUnwind.maxFrames; }
    void setMaxUnwindFrames(int maxUnwindFrames) { m_currentUnwind.maxFrames = maxUnwindFrames; }

    PerfRegisterInfo::Architecture architecture() const { return m_architecture; }
    void setArchitecture(PerfRegisterInfo::Architecture architecture)
    {
        m_architecture = architecture;
    }

    void setByteOrder(QSysInfo::Endian byteOrder) { m_byteOrder = byteOrder; }
    QSysInfo::Endian byteOrder() const { return m_byteOrder; }

    void registerElf(const PerfRecordMmap &mmap);
    void comm(const PerfRecordComm &comm);
    void attr(const PerfRecordAttr &attr);
    void lost(const PerfRecordLost &lost);
    void features(const PerfFeatures &features);
    void tracing(const PerfTracingData &tracingData);
    void finishedRound();
    void contextSwitch(const PerfRecordContextSwitch &contextSwitch);

    bool ipIsInKernelSpace(quint64 ip) const;
    void sample(const PerfRecordSample &sample);

    void fork(const PerfRecordFork &sample);
    void exit(const PerfRecordExit &sample);
    PerfSymbolTable *symbolTable(qint32 pid);
    Dwfl *dwfl(qint32 pid);

    qint32 resolveString(const QByteArray &string);
    qint32 lookupString(const QByteArray &string);

    void addAttributes(const PerfEventAttributes &attributes, const QByteArray &name,
                       const QList<quint64> &ids);

    int lookupLocation(const Location &location) const;
    int resolveLocation(const Location &location);

    bool hasSymbol(int locationId) const;
    void resolveSymbol(int locationId, const Symbol &symbol);

    PerfKallsymEntry findKallsymEntry(quint64 address);
    PerfAddressCache *addressCache() { return &m_addressCache; }

    enum ErrorCode {
        TimeOrderViolation = 1,
        MissingElfFile = 2,
        InvalidKallsyms = 3,
    };
    Q_ENUM(ErrorCode)
    void sendError(ErrorCode error, const QString &message);
    void sendProgress(float percent);

    QString systemRoot() const { return m_systemRoot; }
    QString extraLibsPath() const { return m_extraLibsPath; }
    QString appPath() const { return m_appPath; }
    QString debugPath() const { return m_debugPath; }
    Stats stats() const { return m_stats; }

    void finalize()
    {
        finishedRound();
        flushEventBuffer(0);
    }

private:

    enum CallchainContext {
        PERF_CONTEXT_HV             = static_cast<quint64>(-32),
        PERF_CONTEXT_KERNEL         = static_cast<quint64>(-128),
        PERF_CONTEXT_USER           = static_cast<quint64>(-512),

        PERF_CONTEXT_GUEST          = static_cast<quint64>(-2048),
        PERF_CONTEXT_GUEST_KERNEL   = static_cast<quint64>(-2176),
        PERF_CONTEXT_GUEST_USER     = static_cast<quint64>(-2560),

        PERF_CONTEXT_MAX            = static_cast<quint64>(-4095),
    };

    UnwindInfo m_currentUnwind;
    QIODevice *m_output;

    Dwfl_Callbacks m_offlineCallbacks;
    char *m_debugInfoPath;

    PerfRegisterInfo::Architecture m_architecture;


    // Root of the file system of the machine that recorded the data. Any binaries and debug
    // symbols not found in appPath or extraLibsPath have to appear here.
    QString m_systemRoot;

    // Extra path to search for binaries and debug symbols before considering the system root
    QString m_extraLibsPath;

    // Path where the application being profiled resides. This is the first path to look for
    // binaries and debug symbols.
    QString m_appPath;

    // Path to debug information, e.g. ~/.debug and /usr/local/debug
    QString m_debugPath;

    // Path to kallsyms path
    QString m_kallsymsPath;
    bool m_ignoreKallsymsBuildId;

    QList<PerfRecordSample> m_sampleBuffer;
    QList<PerfRecordMmap> m_mmapBuffer;
    struct TaskEvent
    {
        qint32 m_pid;
        qint32 m_tid;
        quint64 m_time;
        quint32 m_cpu;
        qint32 m_payload;
        EventType m_type;

        quint64 time() const { return m_time; }
        quint64 size() const { return sizeof(TaskEvent); }
    };
    QList<TaskEvent> m_taskEventsBuffer;
    QHash<qint32, PerfSymbolTable *> m_symbolTables;
    PerfKallsyms m_kallsyms;
    PerfAddressCache m_addressCache;
    PerfTracingData m_tracingData;

    QHash<QByteArray, qint32> m_strings;
    QHash<Location, qint32> m_locations;
    QHash<qint32, Symbol> m_symbols;
    QHash<quint64, qint32> m_attributeIds;
    QVector<PerfEventAttributes> m_attributes;
    QHash<QByteArray, QByteArray> m_buildIds;

    uint m_lastEventBufferSize;
    uint m_maxEventBufferSize;
    uint m_targetEventBufferSize;
    uint m_eventBufferSize;

    uint m_timeOrderViolations;

    quint64 m_lastFlushMaxTime;
    QSysInfo::Endian m_byteOrder = QSysInfo::LittleEndian;

    Stats m_stats;

    void unwindStack();
    void resolveCallchain();
    void analyze(const PerfRecordSample &sample);
    void sendBuffer(const QByteArray &buffer);
    void sendString(qint32 id, const QByteArray &string);
    void sendLocation(qint32 id, const Location &location);
    void sendSymbol(qint32 id, const Symbol &symbol);
    void sendAttributes(qint32 id, const PerfEventAttributes &attributes, const QByteArray &name);
    void sendEventFormat(qint32 id, const EventFormat &format);
    void sendTaskEvent(const TaskEvent &taskEvent);

    template<typename Event>
    void bufferEvent(const Event &event, QList<Event> *buffer, uint *eventCounter);
    void flushEventBuffer(uint desiredBufferSize);

    QVariant readTraceData(const QByteArray &data, const FormatField &field, bool byteSwap);
    void forwardMmapBuffer(QList<PerfRecordMmap>::Iterator &it,
                           const QList<PerfRecordMmap>::Iterator &mmapEnd,
                           quint64 timestamp);
    void revertTargetEventBufferSize();
    bool hasTracePointAttributes() const;
};

uint qHash(const PerfUnwind::Location &location, uint seed = 0);
bool operator==(const PerfUnwind::Location &a, const PerfUnwind::Location &b);
