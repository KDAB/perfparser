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
        const auto libfoo_a = QByteArrayLiteral("/usr/lib/libfoo_a.so");
        const auto libfoo_b = QByteArrayLiteral("/usr/lib/libfoo_b.so");

        PerfAddressCache cache;

        QVERIFY(!cache.findSymbol(libfoo_a, 0).isValid());
        QVERIFY(!cache.findSymbol(libfoo_b, 0).isValid());

        cache.setSymbolCache(libfoo_a, {{0x100, 0x100, 10, "Foo"}, {0x11a, 0x11a, 0, "FooZ"}, {0x12a, 0x12a, 10, "FooN"}});
        for (auto addr : {0x100, 0x100 + 9}) {
            const auto cached = cache.findSymbol(libfoo_a, addr);
            QVERIFY(cached.isValid());
            QCOMPARE(cached.offset, quint64(0x100));
            QCOMPARE(cached.size, quint64(10));
            QCOMPARE(cached.symname, "Foo");
        }
        QVERIFY(!cache.findSymbol(libfoo_a, 0x100 + 10).isValid());
        QVERIFY(!cache.findSymbol(libfoo_b, 0x100).isValid());
        QVERIFY(!cache.findSymbol(libfoo_b, 0x100 + 9).isValid());
        QVERIFY(cache.findSymbol(libfoo_a, 0x11a + 1).isValid());
    }
};

QTEST_GUILESS_MAIN(TestAddressCache)

#include "tst_addresscache.moc"
