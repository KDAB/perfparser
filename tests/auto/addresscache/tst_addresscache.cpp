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

#include <QObject>
#include <QTest>
#include <QDebug>
#include <QTemporaryFile>

#include "perfaddresscache.h"

class TestAddressCache : public QObject
{
    Q_OBJECT
private slots:
    void testRelative()
    {
        PerfElfMap::ElfInfo info_a{{}, 0x100, 100, 0,
                                   QByteArrayLiteral("libfoo.so"),
                                   QByteArrayLiteral("/usr/lib/libfoo.so")};
        PerfElfMap::ElfInfo info_b = info_a;
        info_b.addr = 0x200;

        PerfAddressCache cache;
        PerfAddressCache::AddressCacheEntry entry{42, true};
        cache.cache(info_a, 0x110, entry);
        QCOMPARE(cache.find(info_a, 0x110).locationId, entry.locationId);
        QCOMPARE(cache.find(info_b, 0x210).locationId, entry.locationId);
    }

    void testInvalid()
    {
        PerfAddressCache cache;
        PerfAddressCache::AddressCacheEntry entry{42, true};
        cache.cache(PerfElfMap::ElfInfo{}, 0x110, entry);
        QCOMPARE(cache.find(PerfElfMap::ElfInfo{}, 0x110).locationId, entry.locationId);
    }

    void testEmpty()
    {
        PerfAddressCache cache;
        QCOMPARE(cache.find(PerfElfMap::ElfInfo{}, 0x123).locationId, -1);
    }
};

QTEST_GUILESS_MAIN(TestAddressCache)

#include "tst_addresscache.moc"
