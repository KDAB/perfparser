/****************************************************************************
**
** Copyright (C) 2017 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Milian Wolff <milian.wolff@kdab.com>
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

#ifndef PERFKALLSYMS_H
#define PERFKALLSYMS_H

#include <QByteArray>
#include <QVector>

struct PerfKallsymEntry
{
    quint64 address = 0;
    QByteArray symbol;
    QByteArray module;
};

QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(PerfKallsymEntry, Q_MOVABLE_TYPE);
QT_END_NAMESPACE

class PerfKallsyms
{
public:
    PerfKallsyms(const QString &path);

    PerfKallsymEntry findEntry(quint64 address) const;

private:
    QVector<PerfKallsymEntry> m_entries;
};

#endif // PERFKALLSYMS_H

