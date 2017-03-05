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

#include <QDebug>

QDebug operator<<(QDebug stream, const PerfElfMap::ElfInfo& info)
{
    stream.nospace() << "ElfInfo{"
                     << "file=" << info.file.fileName() << ", "
                     << "found=" << info.found << ", "
                     << "addr=" << info.addr << ", "
                     << "len=" << info.length << ", "
                     << "pgoff=" << info.pgoff << ", "
                     << "timeAdded=" << info.timeAdded << ", "
                     << "timeOverwritten=" << info.timeOverwritten << "}";
    return stream.space();
}

bool PerfElfMap::registerElf(const quint64 addr, const quint64 len, quint64 pgoff,
                             const quint64 time, const QFileInfo &fullPath)
{
    bool cacheInvalid = false;
    quint64 overwritten = std::numeric_limits<quint64>::max();
    const quint64 addrEnd = addr + len;
    const bool isFile = fullPath.isFile();

    QMultiMap<quint64, ElfInfo> fragments;
    for (auto i = m_elfs.begin(), end = m_elfs.end(); i != end && i.key() < addrEnd; ++i) {
        const quint64 iEnd = i.key() + i->length;
        if (iEnd <= addr)
            continue;

        if (i->timeOverwritten > time) {
            // Newly added elf overwrites existing one. Mark the existing one as overwritten and
            // reinsert any fragments of it that remain.

            if (i.key() < addr) {
                fragments.insertMulti(i.key(), ElfInfo(i->file, i.key(), addr - i.key(), i->pgoff,
                                                        time, i->timeOverwritten));
            }
            if (iEnd > addrEnd) {
                fragments.insertMulti(addrEnd, ElfInfo(i->file, addrEnd, iEnd - addrEnd,
                                                        i->pgoff + addrEnd - i.key(), time,
                                                        i->timeOverwritten));
            }
            i->timeOverwritten = time;
        }

        // Overlapping module. Clear the cache, but only when the section is actually backed by a
        // file. Otherwise, we will see tons of overlapping heap/anon sections which don't actually
        // invalidate our caches
        if (isFile || i->found)
            cacheInvalid = true;
    }

    m_elfs.unite(fragments);
    m_elfs.insertMulti(addr, ElfInfo(fullPath, addr, len, pgoff, time, overwritten));

    return cacheInvalid;
}

PerfElfMap::ElfInfo PerfElfMap::findElf(quint64 ip, quint64 timestamp) const
{
    QMap<quint64, ElfInfo>::ConstIterator i = m_elfs.upperBound(ip);
    if (i == m_elfs.constEnd() || i.key() != ip) {
        if (i != m_elfs.constBegin())
            --i;
        else
            return {};
    }

    while (true) {
        if (i->timeAdded <= timestamp && i->timeOverwritten > timestamp)
            return (i.key() + i->length > ip) ? i.value() : ElfInfo();

        if (i == m_elfs.constBegin())
            return {};

        --i;
    }
}

bool PerfElfMap::isAddressInRange(quint64 addr) const
{
    if (m_elfs.isEmpty())
        return false;

    const auto &first = m_elfs.first();
    const auto &last = m_elfs.last();
    return first.addr <= addr && addr < (last.addr + last.length);
}
