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

#pragma once

#include <QHash>
#include <QIODevice>
#include <QObject>
#include <QVariant>
#include <QVector>

QT_BEGIN_NAMESPACE
class QTextStream;
QT_END_NAMESPACE

class PerfParserTestClient : public QObject
{
    Q_OBJECT
public:
    struct AttributeEvent {
        quint32 type = 0;
        qint32 name = -1;
        quint64 config = 0;
        bool usesFrequency = false;
        quint64 frequencyOrPeriod = 0;
    };

    struct ThreadEvent {
        qint32 pid = -1;
        qint32 tid = -1;
        quint64 time = 0;
        quint32 cpu = 0;
    };

    struct ThreadEndEvent : public ThreadEvent
    {
    };

    struct CommandEvent : public ThreadEvent {
        qint32 name = -1;
    };

    struct LocationEvent {
        quint64 address = 0;
        qint32 file = -1;
        quint32 pid = 0;
        qint32 line = -1;
        qint32 column = -1;
        qint32 parentLocationId = -1;
    };

    struct SymbolEvent {
        qint32 name = -1;
        qint32 binary = -1;
        qint32 path = -1;
        bool isKernel = false;
    };

    struct SampleEvent : public ThreadEvent {
        QVector<qint32> frames;
        QVector<QPair<qint32, quint64>> values;
        QHash<qint32, QVariant> tracePointData;
        quint8 numGuessedFrames = 0;
    };

    struct TracePointFormatEvent {
        qint32 system = -1;
        qint32 name = -1;
        quint32 flags = 0;
    };

    // Repeated here, as we want to check against accidental changes in enum values.
    enum EventType {
        ThreadStart,
        ThreadEnd,
        Command,
        LocationDefinition,
        SymbolDefinition,
        StringDefinition,
        LostDefinition,
        FeaturesDefinition,
        Error,
        Progress,
        TracePointFormat,
        AttributesDefinition,
        ContextSwitchDefinition,
        Sample,
        TracePointSample,
        InvalidType
    };
    Q_ENUM(EventType)

    explicit PerfParserTestClient(QObject *parent = nullptr);

    void extractTrace(QIODevice *output);

    QByteArray string(qint32 id) const { return m_strings.value(id); }
    CommandEvent command(qint32 tid) const { return m_commands[tid]; }
    AttributeEvent attribute(qint32 id) const { return m_attributes.value(id); }
    QVector<SampleEvent> samples() const { return m_samples; }
    LocationEvent location(qint32 id) const { return m_locations.value(id); }
    SymbolEvent symbol(qint32 id) const { return m_symbols.value(id); }

    TracePointFormatEvent tracePointFormat(qint32 id) const { return m_tracePointFormats.value(id); }

    void convertToText(QTextStream &output) const;

private:
    QVector<QByteArray> m_strings;
    QVector<AttributeEvent> m_attributes;
    QHash<qint32, CommandEvent> m_commands;
    QVector<ThreadEndEvent> m_threadEnds;
    QVector<LocationEvent> m_locations;
    QHash<qint32, SymbolEvent> m_symbols;
    QVector<SampleEvent> m_samples;
    QHash<qint32, TracePointFormatEvent> m_tracePointFormats;
};
