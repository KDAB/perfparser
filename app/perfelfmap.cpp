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

#include "perfelfmap.h"

#include "perfdata.h"

bool PerfElfMap::registerElf(const quint64 addr, const quint64 len,
                         const quint64 time, const QFileInfo &fullPath)
{
    bool cacheInvalid = false;
    quint64 overwritten = std::numeric_limits<quint64>::max();
    for (auto i = m_elfs.begin(), end = m_elfs.end(); i != end
         && i.key() < addr + len; ++i) {
        if (i.key() + i->length <= addr)
            continue;

        if (time > i->timeAdded)
            i->timeOverwritten = qMin(i->timeOverwritten, time);
        else
            overwritten = qMin(i->timeAdded, overwritten);

        // Overlapping module. Clear the cache
        cacheInvalid = true;
    }

    m_elfs.insertMulti(addr, ElfInfo(fullPath, len, time, overwritten,
                                     fullPath.isFile()));

    return cacheInvalid;
}

PerfElfMap::ConstIterator PerfElfMap::findElf(quint64 ip, quint64 timestamp) const
{
    QMap<quint64, ElfInfo>::ConstIterator i = m_elfs.upperBound(ip);
    if (i == m_elfs.constEnd() || i.key() != ip) {
        if (i != m_elfs.constBegin())
            --i;
        else
            return m_elfs.constEnd();
    }

//    /* On ARM, symbols for thumb functions have 1 added to
//     * the symbol address as a flag - remove it */
//    if ((ehdr.e_machine == EM_ARM) &&
//        (map->type == MAP__FUNCTION) &&
//        (sym.st_value & 1))
//        --sym.st_value;
//
//    ^ We don't have to do this here as libdw is supposed to handle it from version 0.160.

    while (true) {
        if (i->timeAdded <= timestamp && i->timeOverwritten > timestamp)
            return (i.key() + i->length > ip) ? i : m_elfs.constEnd();

        if (i == m_elfs.constBegin())
            return m_elfs.constEnd();

        --i;
    }
}

