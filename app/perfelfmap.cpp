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

#include "perfdata.h"
#include "perfelfmap.h"

#include <QDebug>

QDebug operator<<(QDebug stream, const PerfElfMap::ElfInfo& info)
{
    stream.nospace() << "ElfInfo{"
                     << "localFile=" << info.localFile.absoluteFilePath() << ", "
                     << "isFile=" << info.isFile() << ", "
                     << "originalFileName=" << info.originalFileName << ", "
                     << "originalPath=" << info.originalPath << ", "
                     << "addr=" << hex << info.addr << ", "
                     << "len=" << hex << info.length << ", "
                     << "pgoff=" << hex << info.pgoff << ", "
                     << "baseAddr=";

    if (info.hasBaseAddr())
        stream << hex << info.baseAddr;
    else
        stream << "n/a";

    stream << "}";
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

PerfElfMap::PerfElfMap(QObject *parent)
    : QObject(parent)
{
}

PerfElfMap::~PerfElfMap() = default;

void PerfElfMap::registerElf(quint64 addr, quint64 len, quint64 pgoff,
                             const QFileInfo &fullPath, const QByteArray &originalFileName,
                             const QByteArray &originalPath)
{
    quint64 addrEnd = addr + len;

    QVarLengthArray<ElfInfo, 8> newElfs;
    QVarLengthArray<int, 8> removedElfs;
    for (auto i = m_elfs.begin(), end = m_elfs.end(); i != end && i->addr < addrEnd; ++i) {
        const quint64 iEnd = i->addr + i->length;
        if (iEnd < addr)
            continue;

        if (addr - pgoff == i->addr - i->pgoff && originalPath == i->originalPath) {
            // Remapping parts of the same file in the same place: Extend to maximum continuous
            // address range and check if we already have that.
            addr = qMin(addr, i->addr);
            pgoff = qMin(pgoff, i->pgoff);
            addrEnd = qMax(addrEnd, iEnd);
            len = addrEnd - addr;
            if (addr == i->addr && len == i->length) {
                // New mapping is fully contained in old one: Nothing to do.
                Q_ASSERT(newElfs.isEmpty());
                Q_ASSERT(removedElfs.isEmpty());
                return;
            }
        } else if (iEnd == addr) {
            // Directly adjacent sections of the same file can be merged. Ones of different files
            // don't bother each other.
            continue;
        }

        // Newly added elf overwrites existing one. Mark the existing one as overwritten and
        // reinsert any fragments of it that remain.

        if (i->addr < addr) {
            newElfs.push_back(ElfInfo(i->localFile, i->addr, addr - i->addr, i->pgoff,
                                      i->originalFileName, i->originalPath));
        }
        if (iEnd > addrEnd) {
            newElfs.push_back(ElfInfo(i->localFile, addrEnd, iEnd - addrEnd,
                                      i->pgoff + addrEnd - i->addr,
                                      i->originalFileName, i->originalPath));
        }

        aboutToInvalidate(*i);
        removedElfs.push_back(static_cast<int>(std::distance(m_elfs.begin(), i)));
    }

    // remove the overwritten elfs, iterate from the back to not invalidate the indices
    for (auto it = removedElfs.rbegin(), end = removedElfs.rend(); it != end; ++it)
        m_elfs.remove(*it);

    ElfInfo elf(fullPath, addr, len, pgoff, originalFileName, originalPath);

    if (elf.isFile()) {
        if (m_lastBase.originalPath == originalPath && elf.addr > m_lastBase.addr)
            elf.baseAddr = m_lastBase.addr;
        else if (!pgoff)
            m_lastBase = elf;
        else
            m_lastBase = ElfInfo();
    }

    newElfs.push_back(elf);

    for (const auto &elf : newElfs) {
        auto it = std::lower_bound(m_elfs.begin(), m_elfs.end(),
                                   elf.addr, SortByAddr());
        m_elfs.insert(it, elf);
    }
}

PerfElfMap::ElfInfo PerfElfMap::findElf(quint64 ip) const
{
    auto i = std::upper_bound(m_elfs.begin(), m_elfs.end(), ip, SortByAddr());
    Q_ASSERT (i == m_elfs.constEnd() || i->addr != ip);
    if (i != m_elfs.constBegin())
        --i;
    else
        return ElfInfo();

    if (i->dwflStart < i->dwflEnd)
        return (i->dwflStart <= ip && i->dwflEnd > ip) ? *i : ElfInfo();
    else
        return (i->addr + i->length > ip) ? *i : ElfInfo();
}

void PerfElfMap::updateElf(quint64 addr, quint64 dwflStart, quint64 dwflEnd)
{
    auto i = std::upper_bound(m_elfs.begin(), m_elfs.end(), addr, SortByAddr());
    Q_ASSERT(i != m_elfs.begin());
    --i;
    Q_ASSERT(i->addr == addr);
    i->dwflStart = dwflStart;
    i->dwflEnd = dwflEnd;
}

bool PerfElfMap::isAddressInRange(quint64 addr) const
{
    if (m_elfs.isEmpty())
        return false;

    const auto &first = m_elfs.first();
    const auto &last = m_elfs.last();
    return first.addr <= addr && addr < (last.addr + last.length);
}
