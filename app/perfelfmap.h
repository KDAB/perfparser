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

#ifndef PERFELFMAP_H
#define PERFELFMAP_H

#include <QFileInfo>
#include <QMap>

class PerfElfMap
{
public:
    struct ElfInfo {
        ElfInfo(const QFileInfo &file = QFileInfo(), quint64 length = 0, quint64 pgoff = 0,
                quint64 timeAdded = 0,
                quint64 timeOverwritten = std::numeric_limits<quint64>::max(), bool found = true) :
            file(file), length(length), pgoff(pgoff), timeAdded(timeAdded),
            timeOverwritten(timeOverwritten), found(found) {}
        QFileInfo file;
        quint64 length;
        quint64 pgoff;
        quint64 timeAdded;
        quint64 timeOverwritten;
        bool found;
    };

    using Map = QMultiMap<quint64, ElfInfo>;
    using ConstIterator = Map::ConstIterator;

    bool registerElf(quint64 addr, quint64 len, quint64 pgoff, quint64 time,
                     const QFileInfo &fullPath);
    ConstIterator findElf(quint64 ip, quint64 timestamp) const;

    bool isEmpty() const
    {
        return m_elfs.isEmpty();
    }

    ConstIterator constBegin() const
    {
        return m_elfs.constBegin();
    }

    ConstIterator constEnd() const
    {
        return m_elfs.constEnd();
    }

private:
    QMultiMap<quint64, ElfInfo> m_elfs; // needs to be sorted
};

#endif
