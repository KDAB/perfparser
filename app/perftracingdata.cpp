/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd
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

#include "perfattributes.h"
#include "perftracingdata.h"

#include <QBuffer>
#include <QDataStream>
#include <QDebug>

#include <functional>

static QByteArray readNullTerminatedString(QDataStream &stream)
{
    QByteArray string;
    qint8 read = 0;
    while (true) {
        stream >> read;
        if (read != 0)
            string.append(read);
        else
            return string;
    }
}

static bool checkMagic(QDataStream &stream, const QByteArray &magic)
{
    QByteArray read(magic.size(), Qt::Uninitialized);
    stream.readRawData(read.data(), read.size());
    if (read != magic) {
        qWarning() << "Invalid magic in perf tracing data" << read << " - expected" << magic;
        return false;
    }
    return true;
}

template<typename Number>
static bool checkSize(Number size)
{
    if (sizeof(Number) >= sizeof(int) && size > Number(std::numeric_limits<int>::max())) {
        qWarning() << "Excessively large section in tracing data" << size;
        return false;
    }
    return true;
}

const EventFormat &PerfTracingData::eventFormat(qint32 id) const
{
    static EventFormat invalid;
    auto it = m_eventFormats.constFind(id);
    if (it != m_eventFormats.constEnd())
        return *it;
    else
        return invalid;
}

bool PerfTracingData::readHeaderFiles(QDataStream &stream)
{
    if (!checkMagic(stream, QByteArray("header_page") + '\0'))
        return false;

    quint64 size;
    stream >> size;

    if (!checkSize(size))
        return false;

    QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
    stream.readRawData(buffer.data(), buffer.size());

    for (QByteArray line : buffer.split('\n')) {
        if (!line.isEmpty())
            m_headerFields << readFormatField(line);
    }

    if (!checkMagic(stream, QByteArray("header_event") + '\0'))
        return false;

    stream >> size;
    if (!checkSize(size))
        return false;

    stream.skipRawData(static_cast<int>(size));

    return true;
}

static void processLine(const QByteArray &line,
                        const std::function<void(const QByteArray &, const QByteArray &)> &handler)
{
    for (const QByteArray &chunk : line.split('\t')) {
        QList<QByteArray> segments = chunk.split(':');
        if (segments.size() != 2)
            continue;

        QByteArray name = segments[0].toLower();
        QByteArray value = segments[1].trimmed();
        if (value.endsWith(';'))
            value.chop(1);
        handler(name, value);
    }
}

FormatField PerfTracingData::readFormatField(const QByteArray &line)
{
    FormatField field;
    processLine(line, [&](const QByteArray &name, const QByteArray &value) {
        if (name == "field") {
            QList<QByteArray> fieldSegments = value.trimmed().split(' ');
            QByteArray fieldName = fieldSegments.length() > 0 ? fieldSegments.takeLast()
                                                              : QByteArray();
            if (fieldName.startsWith('*')) {
                field.flags |= FIELD_IS_POINTER;
                fieldName.remove(0, 1);
            }
            if (fieldName.endsWith(']')) {
                const int opening = fieldName.lastIndexOf('[');
                if (opening >= 0) {
                    field.flags |= FIELD_IS_ARRAY;
                    field.arraylen = fieldName.mid(opening + 1,
                                                   fieldName.length() - opening - 2).toUInt();
                    fieldName.chop(fieldName.length() - opening);
                }
            }

            field.name = fieldName;
            if (fieldSegments.length() > 0 && fieldSegments.last() == "[]") {
                fieldSegments.removeLast();
                field.flags |= FIELD_IS_ARRAY;
            }
            field.type = fieldSegments.join(' ');
        } else if (name == "offset") {
            field.offset = value.toUInt();
        } else if (name == "size") {
            field.size = value.toUInt();
        } else if (name == "signed") {
            if (value.toInt() != 0)
                field.flags |= FIELD_IS_SIGNED;
        }
    });

    if (field.type.startsWith("__data_loc"))
        field.flags |= FIELD_IS_DYNAMIC;
    if (field.type.contains("long"))
        field.flags |= FIELD_IS_LONG;

    if (field.flags & FIELD_IS_ARRAY) {
        if (field.type.contains("char") || field.type.contains("u8") || field.type.contains("s8")) {
            field.flags |= FIELD_IS_STRING;
            field.elementsize = 1;
        } else if (field.arraylen > 0) {
            field.elementsize = field.size / field.arraylen;
        } else if (field.type.contains("u16") || field.type.contains("s16")) {
            field.elementsize = 2;
        } else if (field.type.contains("u32") || field.type.contains("s32")) {
            field.elementsize = 4;
        } else if (field.type.contains("u64") || field.type.contains("s64")) {
            field.elementsize = 8;
        } else if (field.flags & FIELD_IS_LONG) {
            field.elementsize = m_fileLongSize;
        }
    } else {
        field.elementsize = field.size;
    }
    return field;
}

enum FieldStage {
    BeforeFields,
    CommonFields,
    NonCommonFields,
    AfterFields
};

bool PerfTracingData::readEventFormats(QDataStream &stream, const QByteArray &system)
{
    qint32 count;
    stream >> count;

    for (qint32 x = 0; x < count; ++x) {
        qint32 id = -1;
        bool seenId = false;
        EventFormat event;
        quint64 size;
        stream >> size;
        if (!checkSize(size))
            return false;

        event.system = system;
        if (system == "ftrace")
            event.flags |= EVENT_FL_ISFTRACE;

        QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
        stream.readRawData(buffer.data(), buffer.length());

        FieldStage stage = BeforeFields;
        for (const QByteArray &line : buffer.split('\n')) {
            switch (stage) {
            case CommonFields:
                if (line.isEmpty())
                    stage = NonCommonFields;
                else
                    event.commonFields.append(readFormatField(line));
                break;
            case NonCommonFields:
                if (line.isEmpty())
                    stage = AfterFields;
                else
                    event.fields.append(readFormatField(line));
                break;
            case BeforeFields:
            case AfterFields:
                processLine(line, [&](const QByteArray &name, const QByteArray &value) {
                    if (name == "name") {
                        event.name = value;
                        if ((event.flags & EVENT_FL_ISFTRACE) && value == "bprint")
                            event.flags |= EVENT_FL_ISBPRINT;
                    } else if (name == "id") {
                        id = value.toInt();
                        seenId = true;
                    } else if (name == "format") {
                        stage = CommonFields;
                    }
                });
            }
        }

        if (!seenId) {
            qWarning() << "No ID seen in event format";
            return false;
        }

        m_eventFormats[id] = event;
    }

    return true;
}

bool PerfTracingData::readEventFiles(QDataStream &stream)
{
    qint32 systems;
    stream >> systems;
    for (qint32 i = 0; i < systems; ++i) {
        if (!readEventFormats(stream, readNullTerminatedString(stream)))
            return false;
    }

    return true;
}

bool PerfTracingData::readProcKallsyms(QDataStream &stream)
{
    quint32 size;
    stream >> size;
    if (!checkSize(size))
        return false;
    stream.skipRawData(static_cast<int>(size)); // unused, also in perf
    return true;
}

bool PerfTracingData::readFtracePrintk(QDataStream &stream)
{
    quint32 size;
    stream >> size;

    if (!checkSize(size))
        return false;

    QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
    stream.readRawData(buffer.data(), buffer.length());

    for (QByteArray line : buffer.split('\n')) {
        if (!line.isEmpty()) {
            QList<QByteArray> segments = line.split(':');
            if (segments.length() == 2) {
                QByteArray value = segments[1].trimmed();
                m_ftracePrintk[segments[0].trimmed().toULongLong(nullptr, 0)]
                        = value.mid(1, value.length() - 2);
            }
        }
    }

    return true;
}

bool PerfTracingData::readSavedCmdline(QDataStream &stream)
{
    quint64 size;
    stream >> size;
    if (!checkSize(size))
        return false;

    QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
    stream.readRawData(buffer.data(), buffer.length());

    for (const QByteArray &line : buffer.split('\n')) {
        // Each line is prefixed with the PID it refers to
        if (!line.isEmpty())
            m_savedCmdlines.append(line);
    }

    return true;
}

QDataStream &operator>>(QDataStream &parentStream, PerfTracingData &record)
{
    if (!checkSize(record.m_size)) {
        parentStream.skipRawData(std::numeric_limits<int>::max());
        parentStream.skipRawData(static_cast<int>(record.m_size - std::numeric_limits<int>::max()));
        return parentStream;
    }

    QByteArray data(static_cast<int>(record.m_size), Qt::Uninitialized);
    parentStream.readRawData(data.data(), data.size());
    QDataStream stream(data);

    if (!checkMagic(stream, "\027\bDtracing"))
        return parentStream;

    record.m_version = readNullTerminatedString(stream);

    qint8 read;
    stream >> read;
    record.m_bigEndian = (read != 0);
    stream.setByteOrder(record.m_bigEndian ? QDataStream::BigEndian : QDataStream::LittleEndian);
    stream >> read;
    record.m_fileLongSize = (read != 0);
    stream >> record.m_filePageSize;

    if (!record.readHeaderFiles(stream))
        return parentStream;
    if (!record.readEventFormats(stream, "ftrace"))
        return parentStream;
    if (!record.readEventFiles(stream))
        return parentStream;
    if (!record.readProcKallsyms(stream))
        return parentStream;
    if (!record.readFtracePrintk(stream))
        return parentStream;
    if (record.m_version.toFloat() >= 0.6f) {
        if (!record.readSavedCmdline(stream))
            return parentStream;
    }

    const qint64 padding = record.m_size - stream.device()->pos();
    if (padding >= 8)
        qWarning() << "More trace data left after parsing:" << padding;

    return parentStream;
}
