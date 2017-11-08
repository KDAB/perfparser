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

#include <QObject>
#include <QIODevice>
#include <QVector>

class PerfParserTestClient : public QObject
{
    Q_OBJECT
public:
    struct AttributeEvent {
        quint32 type;
        qint32 name;
        quint64 config;
    };

    struct ThreadEvent {
        qint32 pid;
        qint32 tid;
        quint64 time;
    };

    struct ThreadEndEvent : public ThreadEvent
    {
    };

    struct CommandEvent : public ThreadEvent {
        qint32 name;
    };

    struct LocationEvent {
        quint64 address;
        qint32 file;
        quint32 pid;
        qint32 line;
        qint32 column;
        qint32 parentLocationId;
    };

    struct SymbolEvent {
        qint32 name;
        qint32 binary;
        bool isKernel;
    };

    struct SampleEvent : public ThreadEvent {
        QVector<qint32> frames;
        quint64 period;
        quint64 weight;
        qint32 attributeId;
        quint8 numGuessedFrames;
    };

    // Repeated here, as we want to check against accidental changes in enum values.
    enum EventType {
        Sample43,
        ThreadStart,
        ThreadEnd,
        Command,
        LocationDefinition,
        SymbolDefinition,
        AttributesDefinition,
        StringDefinition,
        LostDefinition,
        FeaturesDefinition,
        Error,
        Sample,
        Progress,
        InvalidType
    };
    Q_ENUM(EventType)

    explicit PerfParserTestClient(QObject *parent = nullptr);

    void extractTrace(QIODevice *output);

    QByteArray string(qint32 id) const { return m_strings.value(id); }
    AttributeEvent attribute(qint32 id) const { return m_attributes.value(id); }
    QVector<SampleEvent> samples() const { return m_samples; }

private:
    QVector<QByteArray> m_strings;
    QVector<AttributeEvent> m_attributes;
    QVector<CommandEvent> m_commands;
    QVector<ThreadEndEvent> m_threadEnds;
    QVector<LocationEvent> m_locations;
    QVector<SymbolEvent> m_symbols;
    QVector<SampleEvent> m_samples;
};
