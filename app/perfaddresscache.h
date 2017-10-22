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

    AddressCacheEntry find(const PerfElfMap::ElfInfo& elf, quint64 addr) const;
    void cache(const PerfElfMap::ElfInfo& elf, quint64 addr,
               const AddressCacheEntry& entry);
    void clearInvalid();
private:
    QHash<QByteArray, QHash<quint64, AddressCacheEntry>> m_cache;
};

#endif
