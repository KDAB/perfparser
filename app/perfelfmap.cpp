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
                     << "localFile=" << info.localFile.absoluteFilePath() << ", "
                     << "isFile=" << info.isFile() << ", "
                     << "originalFileName=" << info.originalFileName << ", "
                     << "originalPath=" << info.originalPath << ", "
                     << "addr=" << hex << info.addr << dec << ", "
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

PerfElfMap::PerfElfMap(QObject *parent)
    : QObject(parent)
{
}

PerfElfMap::~PerfElfMap() = default;

void PerfElfMap::registerElf(const quint64 addr, const quint64 len, quint64 pgoff,
                             const QFileInfo &fullPath, const QByteArray &originalFileName,
                             const QByteArray &originalPath)
{
    const quint64 addrEnd = addr + len;

    QVarLengthArray<ElfInfo, 8> newElfs;
    QVarLengthArray<int, 8> removedElfs;
    for (auto i = m_elfs.begin(), end = m_elfs.end(); i != end && i->addr < addrEnd; ++i) {
        const quint64 iEnd = i->addr + i->length;
        if (iEnd <= addr)
            continue;

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

    newElfs.push_back(ElfInfo(fullPath, addr, len, pgoff, originalFileName, originalPath));

    for (const auto &elf : newElfs) {
        auto it = std::lower_bound(m_elfs.begin(), m_elfs.end(),
                                   elf.addr, SortByAddr());
        m_elfs.insert(it, elf);
    }
}

PerfElfMap::ElfInfo PerfElfMap::findElf(quint64 ip) const
{
    auto i = std::upper_bound(m_elfs.begin(), m_elfs.end(), ip, SortByAddr());
    if (i == m_elfs.constEnd() || i->addr != ip) {
        if (i != m_elfs.constBegin())
            --i;
        else
            return ElfInfo();
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
