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

#ifndef PERFADDRESSCACHE_H
#define PERFADDRESSCACHE_H

#include <QHash>
#include <QVector>

#include "perfelfmap.h"

class PerfAddressCache
{
public:
    struct AddressCacheEntry
    {
        AddressCacheEntry(int locationId = -1, bool isInterworking = false)
            : locationId(locationId)
            , isInterworking(isInterworking)
        {}
        bool isValid() const { return locationId >= 0; }
        int locationId;
        bool isInterworking;
    };
    using OffsetAddressCache = QHash<quint64, AddressCacheEntry>;

    struct SymbolCacheEntry
    {
        SymbolCacheEntry(quint64 offset = 0, quint64 size = 0, const QByteArray &symname = {})
            : offset(offset)
            , size(size)
            , symname(symname)
        {}

        bool isValid() const { return size != 0; }

        quint64 offset;
        quint64 size;
        QByteArray symname;
    };
    using SymbolCache = QVector<SymbolCacheEntry>;

    AddressCacheEntry find(const PerfElfMap::ElfInfo& elf, quint64 addr,
                           OffsetAddressCache *invalidAddressCache) const;
    void cache(const PerfElfMap::ElfInfo& elf, quint64 addr,
               const AddressCacheEntry& entry, OffsetAddressCache *invalidAddressCache);

    SymbolCacheEntry findSymbol(const PerfElfMap::ElfInfo &elf, quint64 addr) const;
    void cacheSymbol(const PerfElfMap::ElfInfo &elf, quint64 startAddr, quint64 size,
                     const QByteArray &symname);
private:
    QHash<QByteArray, OffsetAddressCache> m_cache;
    QHash<QByteArray, SymbolCache> m_symbolCache;
};

QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(PerfAddressCache::SymbolCacheEntry, Q_MOVABLE_TYPE);
QT_END_NAMESPACE
#endif
