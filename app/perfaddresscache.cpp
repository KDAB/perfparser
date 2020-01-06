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

#include "perfaddresscache.h"

namespace {
quint64 relativeAddress(const PerfElfMap::ElfInfo& elf, quint64 addr)
{
    Q_ASSERT(elf.isValid());
    Q_ASSERT(elf.addr <= addr);
    Q_ASSERT((elf.addr + elf.length) > addr);
    return addr - elf.addr;
}
}

PerfAddressCache::AddressCacheEntry PerfAddressCache::find(const PerfElfMap::ElfInfo& elf, quint64 addr,
                                                           OffsetAddressCache *invalidAddressCache) const
{
    if (elf.isValid())
        return m_cache.value(elf.originalPath).value(relativeAddress(elf, addr));
    else
        return invalidAddressCache->value(addr);
}

void PerfAddressCache::cache(const PerfElfMap::ElfInfo& elf, quint64 addr,
                             const PerfAddressCache::AddressCacheEntry& entry,
                             OffsetAddressCache *invalidAddressCache)
{
    if (elf.isValid())
        m_cache[elf.originalPath][relativeAddress(elf, addr)] = entry;
    else
        (*invalidAddressCache)[addr] = entry;
}
