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

#include <libdwfl.h>

#include <QObject>
#include <QList>
#include <QHash>
#include <QIODevice>
#include <QByteArray>
#include <QString>

#include <limits>

class PerfSymbolTable;
class PerfUnwind : public QObject
{
    Q_OBJECT
public:
    enum EventType {
        GoodStack,
        BadStack,
        ThreadStart,
        ThreadEnd,
        Command,
        LocationDefinition,
        SymbolDefinition,
        InvalidType
    };

    struct Location {
        explicit Location(quint64 address = 0, const QByteArray &file = QByteArray(),
                          qint32 line = 0, qint32 column = 0, qint32 parentLocationId = -1) :
            address(address), file(file), line(line), column(column),
            parentLocationId(parentLocationId) {}

        quint64 address;
        QByteArray file;
        qint32 line;
        qint32 column;
        qint32 parentLocationId;
    };

    struct Symbol {
        explicit Symbol(const QByteArray &name = QByteArray(),
                        const QByteArray &binary = QByteArray(), bool isKernel = false) :
            name(name), binary(binary), isKernel(isKernel)
        {}

        QByteArray name;
        QByteArray binary;
        bool isKernel;
    };

    struct UnwindInfo {
        UnwindInfo() : frames(0), unwind(0), sample(0), isInterworking(false), broken(false) {}

        QVector<qint32> frames;
        PerfUnwind *unwind;
        const PerfRecordSample *sample;
        bool isInterworking;
        bool broken;
    };

    static const quint32 s_kernelPid = std::numeric_limits<quint32>::max();
    static const int s_maxFrames = 64;

    PerfUnwind(QIODevice *output, const QString &systemRoot, const QString &debugInfo,
               const QString &extraLibs, const QString &appPath);
    ~PerfUnwind();

    PerfRegisterInfo::Architecture architecture() const { return m_architecture; }
    void setArchitecture(PerfRegisterInfo::Architecture architecture)
    {
        m_architecture = architecture;
    }

    void registerElf(const PerfRecordMmap &mmap);
    void comm(PerfRecordComm &comm);

    Dwfl_Module *reportElf(quint64 ip, quint32 pid, quint64 timestamp);
    bool ipIsInKernelSpace(quint64 ip) const;
    void sample(const PerfRecordSample &sample);

    void fork(const PerfRecordFork &sample);
    void exit(const PerfRecordExit &sample);
    PerfSymbolTable *symbolTable(quint32 pid);
    Dwfl *dwfl(quint32 pid, quint64 timestamp);

    int resolveLocation(const Location &location);

    bool hasSymbol(int locationId) const;
    void resolveSymbol(int locationId, const Symbol &symbol);

private:

    enum CallchainContext {
        PERF_CONTEXT_HV             = (quint64)-32,
        PERF_CONTEXT_KERNEL         = (quint64)-128,
        PERF_CONTEXT_USER           = (quint64)-512,

        PERF_CONTEXT_GUEST          = (quint64)-2048,
        PERF_CONTEXT_GUEST_KERNEL   = (quint64)-2176,
        PERF_CONTEXT_GUEST_USER     = (quint64)-2560,

        PERF_CONTEXT_MAX            = (quint64)-4095,
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

    QList<PerfRecordSample> m_sampleBuffer;
    QHash<quint32, PerfSymbolTable *> m_symbolTables;

    QHash<Location, int> m_locations;
    QHash<int, Symbol> m_symbols;

    uint m_sampleBufferSize;

    static const uint s_maxSampleBufferSize = 1024 * 1024;

    void unwindStack(Dwfl *dwfl);
    void resolveCallchain();
    void analyze(const PerfRecordSample &sample);
    void sendBuffer(const QByteArray &buffer);
    void sendLocation(int id, const Location &location);
    void sendSymbol(int id, const Symbol &symbol);
};

uint qHash(const PerfUnwind::Location &location, uint seed = 0);
bool operator==(const PerfUnwind::Location &a, const PerfUnwind::Location &b);

#endif // PERFUNWIND_H
