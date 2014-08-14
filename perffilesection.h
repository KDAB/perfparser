#ifndef PERFFILESECTION_H
#define PERFFILESECTION_H

#include <QDataStream>

struct PerfFileSection {
    PerfFileSection();
    quint64 offset;
    quint64 size;
};

QDataStream &operator>>(QDataStream &stream, PerfFileSection &section);

#endif // PERFFILESECTION_H
