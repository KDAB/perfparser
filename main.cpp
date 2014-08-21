#include <QFile>
#include <QDebug>
#include <QtEndian>
#include <limits>

#include "perfheader.h"
#include "perfattributes.h"
#include "perffeatures.h"
#include "perfdata.h"
#include "perfunwind.h"

enum ErrorCodes {
    CannotOpen = 1,
    BadMagic,
};


const QLatin1String DefaultFileName("perf.data");

// TODO: parse from parameters
const QByteArray systemRoot("/home/ulf/sysroot");
const QByteArray extraLibs("/home/ulf/Qt/Boot2Qt-3.x/beaglebone-eLinux/qt5");
const QByteArray appPath("/home/ulf/sysroot/usr/bin");

int main(int argc, char *argv[])
{
    QFile file(argc > 1 ? QLatin1String(argv[1]) : DefaultFileName);

    if (!file.open(QIODevice::ReadOnly))
        return CannotOpen;

    PerfHeader header;
    header.read(&file);
    PerfAttributes attributes;
    attributes.read(&file, &header);


    PerfFeatures features;
    qDebug() << file.size();
    features.read(&file, &header);

    PerfData data;
    data.read(&file, &header, &attributes, &features);

    QSet<quint32> pids;
    foreach (const PerfRecordMmap &mmap, data.mmapRecords()) {
        // UINT32_MAX is kernel
        if (mmap.pid() != std::numeric_limits<quint32>::max())
            pids << mmap.pid();
    }

    foreach (quint32 pid, pids) {
        PerfUnwind unwind(pid, &header, &features, systemRoot, extraLibs, appPath);
        foreach (const PerfRecordMmap &mmap, data.mmapRecords()) {
            unwind.registerElf(mmap);
        }

        foreach (const PerfRecordSample &sample, data.sampleRecords()) {
            unwind.unwind(sample);
        }
    }
}





