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
                     << "isFile=" << info.isFile() << ", "
                     << "addr=" << info.addr << ", "
                     << "len=" << info.length << ", "
                     << "pgoff=" << info.pgoff << ", "
                     << "}";
    return stream.space();
}

namespace {
struct SortByAddr
{
    bool operator()(const PerfElfMap::ElfInfo &lhs, const quint64 addr) const
    {
        return lhs.addr < addr;
    }

    bool operator()(const quint64 addr, const PerfElfMap::ElfInfo &rhs) const
    {
        return addr < rhs.addr;
    }
};
}

bool PerfElfMap::registerElf(const quint64 addr, const quint64 len, quint64 pgoff,
                             const QFileInfo &fullPath)
{
    bool cacheInvalid = false;
    const quint64 addrEnd = addr + len;
    const bool isFile = fullPath.isFile();

    QVarLengthArray<ElfInfo, 8> newElfs;
    QVarLengthArray<int, 8> removedElfs;
    for (auto i = m_elfs.begin(), end = m_elfs.end(); i != end && i->addr < addrEnd; ++i) {
        const quint64 iEnd = i->addr + i->length;
        if (iEnd <= addr)
            continue;

        // Newly added elf overwrites existing one. Mark the existing one as overwritten and
        // reinsert any fragments of it that remain.

        if (i->addr < addr) {
            newElfs.push_back(ElfInfo(i->file, i->addr, addr - i->addr,
                                      i->pgoff));
        }
        if (iEnd > addrEnd) {
            newElfs.push_back(ElfInfo(i->file, addrEnd, iEnd - addrEnd,
                                      i->pgoff + addrEnd - i->addr));
        }

        removedElfs.push_back(std::distance(m_elfs.begin(), i));

        // Overlapping module. Clear the cache, but only when the section is actually backed by a
        // file. Otherwise, we will see tons of overlapping heap/anon sections which don't actually
        // invalidate our caches
        if (isFile || i->isFile())
            cacheInvalid = true;
    }

    // remove the overwritten elfs, iterate from the back to not invalidate the indices
    for (auto it = removedElfs.rbegin(), end = removedElfs.rend(); it != end; ++it)
        m_elfs.remove(*it);

    newElfs.push_back(ElfInfo(fullPath, addr, len, pgoff));

    for (const auto &elf : newElfs) {
        auto it = std::lower_bound(m_elfs.begin(), m_elfs.end(),
                                   elf.addr, SortByAddr());
        m_elfs.insert(it, elf);
    }

    return cacheInvalid;
}

PerfElfMap::ElfInfo PerfElfMap::findElf(quint64 ip) const
{
    auto i = std::upper_bound(m_elfs.begin(), m_elfs.end(), ip, SortByAddr());
    if (i == m_elfs.constEnd() || i->addr != ip) {
        if (i != m_elfs.constBegin())
            --i;
        else
            return {};
    }

    return (i->addr + i->length > ip) ? *i : ElfInfo();
}

bool PerfElfMap::isAddressInRange(quint64 addr) const
{
    if (m_elfs.isEmpty())
        return false;

    const auto &first = m_elfs.first();
    const auto &last = m_elfs.last();
    return first.addr <= addr && addr < (last.addr + last.length);
}
