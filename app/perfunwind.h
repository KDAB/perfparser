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

#ifndef PERFUNWIND_H
#define PERFUNWIND_H

#include "perfdata.h"
#include "perfregisterinfo.h"
#include "perfkallsyms.h"

#include <libdwfl.h>

#include <QObject>
#include <QList>
#include <QHash>
#include <QIODevice>
#include <QByteArray>
#include <QString>
#include <QDir>

#include <limits>

class PerfSymbolTable;
class PerfUnwind : public QObject
{
    Q_OBJECT
public:
    enum EventType {
        Sample43, // now obsolete
        ThreadStart,
        ThreadEnd,
        Command,
        LocationDefinition,
        SymbolDefinition,
        AttributesDefinition,
        StringDefinition,
        LostDefinition,
        FeaturesDefinition,
        Error,
        Sample,
        Progress,
        ContextSwitchDefinition,
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
        explicit Symbol(qint32 name = -1, qint32 binary = -1,
                        bool isKernel = false) :
            name(name), binary(binary), isKernel(isKernel)
        {}

        qint32 name;
        qint32 binary;
        bool isKernel;
    };

    struct UnwindInfo {
        UnwindInfo() : frames(0), unwind(0), sample(0), maxFrames(64),
            firstGuessedFrame(-1), isInterworking(false) {}

        QHash<quint32, QHash<quint64, Dwarf_Word>> stackValues;
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
            numSamplesInRound(0), numMmapsInRound(0), numContextSwitchesInRound(0),
            maxSamplesPerRound(0), maxMmapsPerRound(0), maxContextSwitchesPerRound(0),
            maxSamplesPerFlush(0), maxMmapsPerFlush(0), maxContextSwitchesPerFlush(0),
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
        uint numContextSwitchesInRound;
        uint maxSamplesPerRound;
        uint maxMmapsPerRound;
        uint maxContextSwitchesPerRound;
        uint maxSamplesPerFlush;
        uint maxMmapsPerFlush;
        uint maxContextSwitchesPerFlush;
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
    void setMaxEventBufferSize(uint size) { m_maxEventBufferSize = size; }

    int maxUnwindFrames() const { return m_currentUnwind.maxFrames; }
    void setMaxUnwindFrames(int maxUnwindFrames) { m_currentUnwind.maxFrames = maxUnwindFrames; }

    PerfRegisterInfo::Architecture architecture() const { return m_architecture; }
    void setArchitecture(PerfRegisterInfo::Architecture architecture)
    {
        m_architecture = architecture;
    }

    void registerElf(const PerfRecordMmap &mmap);
    void comm(const PerfRecordComm &comm);
    void attr(const PerfRecordAttr &attr);
    void lost(const PerfRecordLost &lost);
    void features(const PerfFeatures &features);
    void finishedRound();
    void contextSwitch(const PerfRecordContextSwitch &contextSwitch);

    Dwfl_Module *reportElf(quint64 ip, qint32 pid);
    bool ipIsInKernelSpace(quint64 ip) const;
    void sample(const PerfRecordSample &sample);

    void fork(const PerfRecordFork &sample);
    void exit(const PerfRecordExit &sample);
    PerfSymbolTable *symbolTable(qint32 pid);
    Dwfl *dwfl(qint32 pid);

    qint32 resolveString(const QByteArray &string);

    void addAttributes(const PerfEventAttributes &attributes, const QByteArray &name,
                       const QList<quint64> &ids);

    int lookupLocation(const Location &location) const;
    int resolveLocation(const Location &location);

    bool hasSymbol(int locationId) const;
    void resolveSymbol(int locationId, const Symbol &symbol);

    PerfKallsymEntry findKallsymEntry(quint64 address);

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
    QList<PerfRecordContextSwitch> m_contextSwitchBuffer;
    QHash<qint32, PerfSymbolTable *> m_symbolTables;
    PerfKallsyms m_kallsyms;

    QHash<QByteArray, qint32> m_strings;
    QHash<Location, qint32> m_locations;
    QHash<qint32, Symbol> m_symbols;
    QHash<quint64, qint32> m_attributeIds;
    QVector<PerfEventAttributes> m_attributes;
    QHash<QByteArray, QByteArray> m_buildIds;

    uint m_maxEventBufferSize;
    uint m_eventBufferSize;
    quint64 m_lastFlushMaxTime;

    Stats m_stats;

    void unwindStack(Dwfl *dwfl);
    void resolveCallchain();
    void analyze(const PerfRecordSample &sample);
    void sendBuffer(const QByteArray &buffer);
    void sendString(qint32 id, const QByteArray &string);
    void sendLocation(qint32 id, const Location &location);
    void sendSymbol(qint32 id, const Symbol &symbol);
    void sendAttributes(qint32 id, const PerfEventAttributes &attributes, const QByteArray &name);
    void sendContextSwitch(const PerfRecordContextSwitch &contextSwitch);

    template<typename Event>
    void bufferEvent(const Event &event, QList<Event> *buffer, uint *eventCounter);
    void flushEventBuffer(uint desiredBufferSize);
};

uint qHash(const PerfUnwind::Location &location, uint seed = 0);
bool operator==(const PerfUnwind::Location &a, const PerfUnwind::Location &b);

#endif // PERFUNWIND_H
