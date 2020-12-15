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
        SymbolCacheEntry(quint64 offset = 0, quint64 value = 0, quint64 size = 0, const QByteArray &symname = {})
            : offset(offset)
            , value(value)
            , size(size)
            , symname(symname)
        {}

        bool isValid() const { return !symname.isEmpty(); }

        // adjusted/absolute st_value, see documentation of the `addr` arg in `dwfl_module_getsym_info`
        quint64 offset;
        // unadjusted/relative st_value
        quint64 value;
        quint64 size;
        QByteArray symname;
        bool demangled = false;
    };
    using SymbolCache = QVector<SymbolCacheEntry>;

    AddressCacheEntry find(const PerfElfMap::ElfInfo& elf, quint64 addr,
                           OffsetAddressCache *invalidAddressCache) const;
    void cache(const PerfElfMap::ElfInfo& elf, quint64 addr,
               const AddressCacheEntry& entry, OffsetAddressCache *invalidAddressCache);

    /// check if @c setSymbolCache was called for @p filePath already
    bool hasSymbolCache(const QByteArray &filePath) const;
    /// take @p cache, sort it and use it for symbol lookups in @p filePath
    void setSymbolCache(const QByteArray &filePath, SymbolCache cache);
    /// find the symbol that encompasses @p relAddr in @p filePath
    /// if the found symbol wasn't yet demangled, it will be demangled now
    SymbolCacheEntry findSymbol(const QByteArray &filePath, quint64 relAddr);
private:
    QHash<QByteArray, OffsetAddressCache> m_cache;
    QHash<QByteArray, SymbolCache> m_symbolCache;
};

QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(PerfAddressCache::SymbolCacheEntry, Q_MOVABLE_TYPE);
QT_END_NAMESPACE
#endif
