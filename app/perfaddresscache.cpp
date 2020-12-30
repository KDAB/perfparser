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

#include "perfdwarfdiecache.h"

#include <algorithm>

namespace {
quint64 relativeAddress(const PerfElfMap::ElfInfo& elf, quint64 addr)
{
    Q_ASSERT(elf.isValid());
    const auto elfAddr = elf.hasBaseAddr() ? elf.baseAddr : elf.addr;
    Q_ASSERT(elfAddr <= addr);
    Q_ASSERT((elf.addr + elf.length) > addr);
    return addr - elfAddr;
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

static bool operator<(const PerfAddressCache::SymbolCacheEntry &lhs, const PerfAddressCache::SymbolCacheEntry &rhs)
{
    return lhs.offset < rhs.offset;
}

static bool operator==(const PerfAddressCache::SymbolCacheEntry &lhs, const PerfAddressCache::SymbolCacheEntry &rhs)
{
    return lhs.offset == rhs.offset && lhs.size == rhs.size;
}

static bool operator<(const PerfAddressCache::SymbolCacheEntry &lhs, quint64 addr)
{
    return lhs.offset < addr;
}


bool PerfAddressCache::hasSymbolCache(const QByteArray &filePath) const
{
    return m_symbolCache.contains(filePath);
}

PerfAddressCache::SymbolCacheEntry PerfAddressCache::findSymbol(const QByteArray& filePath, quint64 relAddr)
{
    auto &symbols = m_symbolCache[filePath];
    auto it = std::lower_bound(symbols.begin(), symbols.end(), relAddr);

    // demangle symbols on demand instead of demangling all symbols directly
    // hopefully most of the symbols we won't ever encounter after all
    auto lazyDemangle = [](PerfAddressCache::SymbolCacheEntry& entry) {
        if (!entry.demangled) {
            entry.symname = demangle(entry.symname);
            entry.demangled = true;
        }
        return entry;
    };

    if (it != symbols.end() && it->offset == relAddr)
        return lazyDemangle(*it);
    if (it == symbols.begin())
        return {};

    --it;

    if (it->offset <= relAddr && (it->offset + it->size > relAddr || (it->size == 0))) {
        return lazyDemangle(*it);
    }
    return {};
}

void PerfAddressCache::setSymbolCache(const QByteArray &filePath, SymbolCache cache)
{
    /*
     * use stable_sort to produce results that are comparable to what addr2line would
     * return when we have entries like this in the symtab:
     *
     * 000000000045a130 l     F .text  0000000000000033 .hidden __memmove_avx_unaligned
     * 000000000045a180 l     F .text  00000000000003d8 .hidden __memmove_avx_unaligned_erms
     * 000000000045a180 l     F .text  00000000000003d8 .hidden __memcpy_avx_unaligned_erms
     * 000000000045a130 l     F .text  0000000000000033 .hidden __memcpy_avx_unaligned
     *
     * here, addr2line would always find the first entry. we want to do the same
     */

    std::stable_sort(cache.begin(), cache.end());
    cache.erase(std::unique(cache.begin(), cache.end()), cache.end());
    m_symbolCache[filePath] = cache;
}
