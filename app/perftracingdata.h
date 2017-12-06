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

#pragma once

#include <QByteArray>
#include <QHash>
#include <QVector>

enum FormatFlags: quint32
{
    FIELD_IS_ARRAY    = 1 << 0,
    FIELD_IS_POINTER  = 1 << 1,
    FIELD_IS_SIGNED   = 1 << 2,
    FIELD_IS_STRING   = 1 << 3,
    FIELD_IS_DYNAMIC  = 1 << 4,
    FIELD_IS_LONG     = 1 << 5,
    FIELD_IS_FLAG     = 1 << 6,
    FIELD_IS_SYMBOLIC = 1 << 7,
};

struct FormatField
{
    QByteArray type;
    QByteArray name;
    quint32 offset = 0;
    quint32 size = 0;
    quint32 arraylen = 0;
    quint32 elementsize = 0;
    quint32 flags = 0;
};

enum EventFormatFlags {
    EVENT_FL_ISFTRACE  = 0x01,
    EVENT_FL_ISPRINT   = 0x02,
    EVENT_FL_ISBPRINT  = 0x04,
    EVENT_FL_ISFUNCENT = 0x10,
    EVENT_FL_ISFUNCRET = 0x20,
    EVENT_FL_NOHANDLE  = 0x40,
    EVENT_FL_PRINTRAW  = 0x80,

    EVENT_FL_FAILED    = 0x80000000
};

struct EventFormat
{
    QByteArray name;
    QByteArray system;
    QVector<FormatField> commonFields;
    QVector<FormatField> fields;
    quint32 flags = 0;
};

class PerfTracingData
{
public:
    quint32 size() const { return m_size; }
    void setSize(quint32 size) { m_size = size; }
    QByteArray version() const { return m_version; }
    const EventFormat &eventFormat(qint32 id) const;
    const QHash<qint32, EventFormat> &eventFormats() const {return m_eventFormats; }

private:
    bool readHeaderFiles(QDataStream &stream);
    bool readFtraceFiles(QDataStream &stream);
    bool readEventFiles(QDataStream &stream);
    bool readProcKallsyms(QDataStream &stream);
    bool readFtracePrintk(QDataStream &stream);
    bool readSavedCmdline(QDataStream &stream);
    bool readEventFormats(QDataStream &stream, const QByteArray &system);

    FormatField readFormatField(const QByteArray &line);

    quint32 m_size = 0;
    QByteArray m_version;
    bool m_bigEndian = false;
    bool m_fileLongSize = false;
    qint32 m_filePageSize = false;

    QHash<qint32, EventFormat> m_eventFormats;
    QVector<FormatField> m_headerFields;
    QHash<quint64, QByteArray> m_ftracePrintk;
    QVector<QByteArray> m_savedCmdlines;

    friend QDataStream &operator>>(QDataStream &stream, PerfTracingData &record);
};

QDataStream &operator>>(QDataStream &stream, PerfTracingData &record);
