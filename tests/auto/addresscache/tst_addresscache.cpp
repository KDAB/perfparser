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
        PerfAddressCache::OffsetAddressCache invalidAddressCache;
        PerfAddressCache::AddressCacheEntry entry{42, true};
        cache.cache(info_a, 0x110, entry, &invalidAddressCache);
        QCOMPARE(cache.find(info_a, 0x110, &invalidAddressCache).locationId, entry.locationId);
        QCOMPARE(cache.find(info_b, 0x210, &invalidAddressCache).locationId, entry.locationId);
    }

    void testInvalid()
    {
        PerfAddressCache cache;
        PerfAddressCache::OffsetAddressCache invalidAddressCache_a;
        PerfAddressCache::OffsetAddressCache invalidAddressCache_b;
        PerfAddressCache::AddressCacheEntry entry{42, true};
        cache.cache(PerfElfMap::ElfInfo{}, 0x110, entry, &invalidAddressCache_a);
        QCOMPARE(cache.find(PerfElfMap::ElfInfo{}, 0x110, &invalidAddressCache_a).locationId, entry.locationId);
        QCOMPARE(cache.find(PerfElfMap::ElfInfo{}, 0x110, &invalidAddressCache_b).locationId, -1);
    }

    void testEmpty()
    {
        PerfAddressCache cache;
        PerfAddressCache::OffsetAddressCache invalidAddressCache;
        QCOMPARE(cache.find(PerfElfMap::ElfInfo{}, 0x123, &invalidAddressCache).locationId, -1);
    }

    void testSymbolCache()
    {
        PerfElfMap::ElfInfo info_a{{}, 0x100, 100, 0,
                                   QByteArrayLiteral("libfoo_a.so"),
                                   QByteArrayLiteral("/usr/lib/libfoo_a.so")};
        PerfElfMap::ElfInfo info_a_offset{{}, 0x200, 100, 0x100,
                                   QByteArrayLiteral("libfoo_a.so"),
                                   QByteArrayLiteral("/usr/lib/libfoo_a.so")};
        info_a_offset.baseAddr = info_a.addr;
        PerfElfMap::ElfInfo info_b{{}, 0x100, 100, 0,
                                   QByteArrayLiteral("libfoo_b.so"),
                                   QByteArrayLiteral("/usr/lib/libfoo_b.so")};

        PerfAddressCache cache;

        QVERIFY(!cache.findSymbol(info_a, 0x100).isValid());
        QVERIFY(!cache.findSymbol(info_a_offset, 0x100).isValid());
        QVERIFY(!cache.findSymbol(info_a_offset, 0x200).isValid());
        QVERIFY(!cache.findSymbol(info_b, 0x100).isValid());

        cache.cacheSymbol(info_a, 0x100, 10, "Foo");
        for (auto addr : {0x100, 0x100 + 9}) {
            const auto cached = cache.findSymbol(info_a, addr);
            QVERIFY(cached.isValid());
            QCOMPARE(int(cached.offset), 0);
            QCOMPARE(int(cached.size), 10);
            QCOMPARE(cached.symname, "Foo");
            const auto cached2 = cache.findSymbol(info_a_offset, addr);
            QCOMPARE(cached2.isValid(), cached.isValid());
            QCOMPARE(cached2.offset, cached.offset);
            QCOMPARE(cached2.size, cached.size);
            QCOMPARE(cached2.symname, cached.symname);
        }
        QVERIFY(!cache.findSymbol(info_a, 0x100 + 10).isValid());
        QVERIFY(!cache.findSymbol(info_a_offset, 0x100 + 10).isValid());
        QVERIFY(!cache.findSymbol(info_b, 0x100).isValid());
        QVERIFY(!cache.findSymbol(info_b, 0x100 + 9).isValid());
    }
};

QTEST_GUILESS_MAIN(TestAddressCache)

#include "tst_addresscache.moc"
