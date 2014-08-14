#include <QFile>
#include <QDebug>
#include <QtEndian>

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

    PerfUnwind unwind(&header, &features);
    foreach (const PerfRecordSample &sample, data.sampleRecords()) {
        unwind.unwind(sample);
    }

    qDebug() << (void *)(&main);
    qDebug() << (void *)(&PerfUnwind::unwind);
}





