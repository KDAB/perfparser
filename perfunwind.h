#ifndef PERFUNWIND_H
#define PERFUNWIND_H

#include <elfutils/libdwfl.h>
#include "perfdata.h"

class PerfUnwind
{
private:
    const PerfHeader *header;
    const PerfFeatures *features;
    Dwfl *dwfl;
    uint registerArch;
    QByteArray systemRoot;
    QByteArray extraLibs;
    QByteArray appPath;
    quint32 pid;

public:
    PerfUnwind(const PerfHeader *header, const PerfFeatures *features, quint32 pid);
    ~PerfUnwind();

    void setSystemRoot(const QByteArray &root) { systemRoot = root; }
    void report(const PerfRecordMmap &mmap);
    void unwind(const PerfRecordSample &sample);

};

#endif // PERFUNWIND_H
