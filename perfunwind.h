#ifndef PERFUNWIND_H
#define PERFUNWIND_H

#include <elfutils/libdwfl.h>
#include "perfdata.h"
#include <QFileInfo>
#include <QHash>

class PerfUnwind
{
private:
    quint32 pid;
    const PerfHeader *header;
    const PerfFeatures *features;
    Dwfl *dwfl;
    Dwfl_Callbacks offlineCallbacks;
    char *debugInfoPath;

    uint registerArch;


    // Root of the file system of the machine that recorded the data. Any binaries and debug
    // symbols not found in appPath or extraLibsPath have to appear here.
    QByteArray systemRoot;

    // Extra path to search for binaries and debug symbols before considering the system root
    QByteArray extraLibsPath;

    // Path where the application being profiled resides. This is the first path to look for
    // binaries and debug symbols.
    QByteArray appPath;

    QHash<quint64, QFileInfo> elfs;

public:
    PerfUnwind(quint32 pid, const PerfHeader *header, const PerfFeatures *features,
               const QByteArray &systemRoot, const QByteArray &extraLibs,
               const QByteArray &appPath);
    ~PerfUnwind();


    void report(const PerfRecordMmap &mmap);
    void unwind(const PerfRecordSample &sample);

};

#endif // PERFUNWIND_H
