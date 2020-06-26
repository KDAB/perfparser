/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd
** Contact: http://www.qt.io/licensing/
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

#include "perffeatures.h"
#include "perfparsertestclient.h"

#include <QTextStream>
#include <QtEndian>

#ifdef MANUAL_TEST
#define QVERIFY Q_ASSERT
#define QCOMPARE(x, y) Q_ASSERT(x == y)
#else
#include <QtTest>
#endif

PerfParserTestClient::PerfParserTestClient(QObject *parent) : QObject(parent)
{
}

void PerfParserTestClient::extractTrace(QIODevice *device)
{
    QVERIFY(device->bytesAvailable() > 0);
    const char streamMagic[] = "QPERFSTREAM";
    const int magicSize = sizeof(streamMagic);

    QVarLengthArray<char> magic(magicSize);
    device->read(magic.data(), magicSize);
    QCOMPARE(QByteArray(magic.data(), magic.size()), QByteArray(streamMagic, magicSize));

    qint32 version;
    device->read(reinterpret_cast<char *>(&version), sizeof(qint32));
    version = qFromLittleEndian(version);

    QVERIFY(version == QDataStream::Qt_DefaultCompiledVersion);

    float progress = -1;

    auto checkString = [this](qint32 id) {
        QVERIFY(id < m_strings.length());
        QVERIFY(!m_strings[id].isEmpty());
    };

    auto checkLocation = [this](qint32 id) {
        QVERIFY(id < m_locations.length());
        QVERIFY(m_locations[id].pid != 0);
    };

    auto checkAttribute = [this, &checkString](qint32 id) {
        QVERIFY(id < m_attributes.length());
        checkString(m_attributes[id].name);
    };

    while (device->bytesAvailable() >= static_cast<qint64>(sizeof(quint32))) {
        qint32 size;
        device->read(reinterpret_cast<char *>(&size), sizeof(quint32));
        size = qFromLittleEndian(size);

        QVERIFY(device->bytesAvailable() >= size);
        QDataStream stream(device->read(size));

        quint8 eventType;
        stream >> eventType;

        switch (eventType) {
        case ThreadEnd: {
            ThreadEndEvent threadEnd;
            stream >> threadEnd.pid >> threadEnd.tid >> threadEnd.time >> threadEnd.cpu;
            m_threadEnds.append(threadEnd);
            break;
        }
        case Command: {
            CommandEvent command;
            stream >> command.pid >> command.tid >> command.time >> command.cpu >> command.name;
            checkString(command.name);
            m_commands.insert(command.tid, command);
            break;
        }
        case LocationDefinition: {
            qint32 id;
            LocationEvent location;
            stream >> id >> location.address >> location.file >> location.pid >> location.line
                   >> location.column >> location.parentLocationId;
            if (location.file != -1)
                checkString(location.file);
            if (location.parentLocationId != -1)
                checkLocation(location.parentLocationId);
            QCOMPARE(id, m_locations.length());
            m_locations.append(location);
            break;
        }
        case SymbolDefinition: {
            qint32 id;
            SymbolEvent symbol;
            stream >> id >> symbol.name >> symbol.binary >> symbol.path >> symbol.isKernel;
            if (symbol.name != -1)
                checkString(symbol.name);
            if (symbol.binary != -1)
                checkString(symbol.binary);
            QVERIFY(id < m_locations.size());
            m_symbols.insert(id, symbol);
            break;
        }
        case AttributesDefinition: {
            qint32 id;
            AttributeEvent attribute;
            stream >> id >> attribute.type >> attribute.config >> attribute.name
                   >> attribute.usesFrequency >> attribute.frequencyOrPeriod;
            checkString(attribute.name);
            QCOMPARE(id, m_attributes.length());
            m_attributes.append(attribute);
            break;
        }
        case StringDefinition: {
            qint32 id;
            QByteArray string;
            stream >> id >> string;
            QCOMPARE(id, m_strings.length());
            m_strings.append(string);
            break;
        }
        case Error: {
            qint32 errorCode;
            QString message;
            stream >> errorCode >> message;
            // Ignore this: We cannot find the elfs of course.
            break;
        }
        case Sample:
        case TracePointSample: {
            SampleEvent sample;
            stream >> sample.pid >> sample.tid >> sample.time >> sample.cpu >> sample.frames
                   >> sample.numGuessedFrames >> sample.values;
            for (qint32 locationId : qAsConst(sample.frames))
                checkLocation(locationId);
            for (const auto &value : qAsConst(sample.values))
                checkAttribute(value.first);

            if (eventType == TracePointSample) {
                stream >> sample.tracePointData;
                for (auto it = sample.tracePointData.constBegin(),
                     end = sample.tracePointData.constEnd();
                     it != end; ++it) {
                    checkString(it.key());
                }
            }

            m_samples.append(sample);
            break;
        }
        case Progress: {
            const float oldProgress = progress;
            stream >> progress;
            QVERIFY(progress > oldProgress);
            break;
        }
        case TracePointFormat: {
            qint32 id;
            TracePointFormatEvent tracePointFormat;
            stream >> id >> tracePointFormat.system >> tracePointFormat.name
                   >> tracePointFormat.flags;
            checkString(tracePointFormat.system);
            checkString(tracePointFormat.name);
            QVERIFY(!m_tracePointFormats.contains(id));
            m_tracePointFormats.insert(id, tracePointFormat);
            break;
        }
        default:
            stream.skipRawData(size);
            break;
        }
        QVERIFY(stream.atEnd());
    }
}

void PerfParserTestClient::convertToText(QTextStream &out) const
{
    using Qt::dec;
    using Qt::hex;
    for (const auto &sample : samples()) {
        out << string(command(sample.pid).name) << '\t'
            << sample.pid << '\t' << sample.tid << '\t'
            << sample.time / 1000000000 << '.' << qSetFieldWidth(9) << qSetPadChar(QLatin1Char('0'))
            << sample.time % 1000000000 << qSetFieldWidth(0) << qSetPadChar(QLatin1Char(' ')) << '\n';
        for (const auto &value : sample.values) {
            const auto attribute = this->attribute(value.first);
            const auto cost = attribute.usesFrequency ? value.second : attribute.frequencyOrPeriod;
            out << '\t' << string(attribute.name) << ": ";
            if (attribute.type == 2) {
                const auto format = tracePointFormat(static_cast<qint32>(attribute.config));
                out << string(format.system) << ' ' << string(format.name) << ' ' << Qt::hex << format.flags << Qt::dec << '\n';
                for (auto it = sample.tracePointData.begin(); it != sample.tracePointData.end(); ++it) {
                    out << "\t\t" << string(it.key()) << '=' << it.value().toString() << '\n';
                }
            } else {
                out << cost << '\n';
            }
        }
        out << '\n';
        auto printFrame = [&out, this](qint32 locationId) -> qint32 {
            const auto location = this->location(locationId);
            out << '\t' << Qt::hex << location.address << Qt::dec;
            const auto symbol = this->symbol(locationId);
            if (location.file != -1)
                out << '\t' << string(location.file) << ':' << location.line << ':' << location.column;
            if (symbol.path != -1)
                out << '\t' << string(symbol.name) << ' ' << string(symbol.binary) << ' ' << string(symbol.path) << ' ' << (symbol.isKernel ? "[kernel]" : "");
            out << '\n';
            return location.parentLocationId;
        };
        for (const auto &frame : sample.frames) {
            auto locationId = printFrame(frame);
            while (locationId != -1)
                locationId = printFrame(locationId);
        }
        out << '\n';
    }
}
