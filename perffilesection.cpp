#include "perffilesection.h"

QDataStream &operator>>(QDataStream &stream, PerfFileSection &section)
{
    return stream >> section.offset >> section.size;
}


PerfFileSection::PerfFileSection() : offset(0), size(0) {}
