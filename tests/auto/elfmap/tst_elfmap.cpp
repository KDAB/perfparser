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

#include "perfelfmap.h"

#include <QDebug>
#include <QObject>
#include <QTemporaryFile>
#include <QTest>

QT_BEGIN_NAMESPACE
namespace QTest {
template<>
char *toString(const PerfElfMap::ElfInfo &info)
{
    QString string;
    QDebug stream(&string);
    stream << info;
    return qstrdup(qPrintable(string));
}
}
QT_END_NAMESPACE

class TestElfMap : public QObject
{
    Q_OBJECT
private slots:
    void testNoOverlap()
    {
        const PerfElfMap::ElfInfo invalid;

        PerfElfMap map;
        QVERIFY(map.isEmpty());

        const PerfElfMap::ElfInfo first({}, 100, 10, 0, "foo", "/foo");

        QVERIFY(registerElf(&map, first).isEmpty());
        QVERIFY(!map.isEmpty());

        QCOMPARE(map.findElf(99), invalid);
        QCOMPARE(map.findElf(100), first);
        QCOMPARE(map.findElf(105), first);
        QCOMPARE(map.findElf(109), first);
        QCOMPARE(map.findElf(110), invalid);

        const PerfElfMap::ElfInfo second({}, 0, 10, 0, "bar", "/bar");
        QVERIFY(registerElf(&map, second).isEmpty());

        QCOMPARE(map.findElf(0), second);
        QCOMPARE(map.findElf(5), second);
        QCOMPARE(map.findElf(9), second);
        QCOMPARE(map.findElf(10), invalid);

        QCOMPARE(map.findElf(99), invalid);
        QCOMPARE(map.findElf(100), first);
        QCOMPARE(map.findElf(105), first);
        QCOMPARE(map.findElf(109), first);
        QCOMPARE(map.findElf(110), invalid);
    }

    void testOverwrite()
    {
        QFETCH(bool, firstIsFile);
        QFETCH(bool, secondIsFile);

        QTemporaryFile tmpFile1;
        if (firstIsFile) {
            QVERIFY(tmpFile1.open());
        }
        QFileInfo file1(tmpFile1.fileName());
        QCOMPARE(file1.isFile(), firstIsFile);

        QTemporaryFile tmpFile2;
        if (secondIsFile) {
            QVERIFY(tmpFile2.open());
        }
        QFileInfo file2(tmpFile2.fileName());
        QCOMPARE(file2.isFile(), secondIsFile);

        PerfElfMap map;

        const PerfElfMap::ElfInfo first(file1, 95, 20, 0);
        QVERIFY(registerElf(&map, first).isEmpty());
        QCOMPARE(map.findElf(110), first);

        PerfElfMap::ElfInfo second(file1, 105, 20, 0);
        QCOMPARE(registerElf(&map, second), QVector<PerfElfMap::ElfInfo>{first});
        if (firstIsFile)
            second.baseAddr = first.addr;
        QCOMPARE(map.findElf(110), second);

        const PerfElfMap::ElfInfo fragment1(file1, 95, 10, 0);
        QCOMPARE(map.findElf(97), fragment1);

        const PerfElfMap::ElfInfo third(file2, 100, 20, 0);
        QVector<PerfElfMap::ElfInfo> invalidatedByThird = {fragment1, second};
        QCOMPARE(registerElf(&map, third), invalidatedByThird);
        QCOMPARE(map.findElf(110), third);
        QCOMPARE(map.findElf(110), third);

        const PerfElfMap::ElfInfo fragment2(file1, 120, 5, 15);
        const PerfElfMap::ElfInfo fragment3(file1, 95, 5, 0);
        QCOMPARE(map.findElf(122), fragment2);
        QCOMPARE(map.findElf(97), fragment3);
    }

    void testOverwrite_data()
    {
        QTest::addColumn<bool>("firstIsFile");
        QTest::addColumn<bool>("secondIsFile");

        QTest::newRow("both-files") << true << true;
        QTest::newRow("one-file-A") << false << true;
        QTest::newRow("one-file-B") << true << false;
        QTest::newRow("no-files") << false << false;
    }

    void testIsAddressInRange()
    {
        PerfElfMap map;
        QVERIFY(!map.isAddressInRange(10));

        const PerfElfMap::ElfInfo first({}, 10, 10, 0);
        QVERIFY(registerElf(&map, first).isEmpty());
        QVERIFY(!map.isAddressInRange(9));
        QVERIFY(map.isAddressInRange(10));
        QVERIFY(map.isAddressInRange(19));
        QVERIFY(!map.isAddressInRange(20));

        const PerfElfMap::ElfInfo second({}, 30, 10, 0);
        QVERIFY(registerElf(&map, second).isEmpty());
        QVERIFY(!map.isAddressInRange(9));
        QVERIFY(map.isAddressInRange(10));
        QVERIFY(map.isAddressInRange(19));
        QVERIFY(map.isAddressInRange(30));
        QVERIFY(map.isAddressInRange(39));
        QVERIFY(!map.isAddressInRange(40));
        // gaps are also within range
        QVERIFY(map.isAddressInRange(20));
        QVERIFY(map.isAddressInRange(29));
    }

    void testExtendMapping()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        const auto fileInfo = QFileInfo(file.fileName());

        PerfElfMap map;
        const PerfElfMap::ElfInfo first(fileInfo, 0, 5000, 0);
        registerElf(&map, first);
        QCOMPARE(map.findElf(100), first);

        // fully contained in the first mapping
        const PerfElfMap::ElfInfo second(fileInfo, 20, 500, 20);
        registerElf(&map, second);
        QCOMPARE(map.findElf(100), first);

        // extend the first mapping
        const PerfElfMap::ElfInfo third(fileInfo, 2000, 8000, 2000);
        registerElf(&map, third);
        const PerfElfMap::ElfInfo extended(fileInfo, 0, 10000, 0);
        QCOMPARE(map.findElf(100), extended);
        QCOMPARE(map.findElf(2200), extended);

        // this has a gap, so don't extend directly
        PerfElfMap::ElfInfo fourth(fileInfo, 12000, 100, 100);
        registerElf(&map, fourth);
        QVERIFY(!fourth.hasBaseAddr());
        fourth.baseAddr = 0;
        QVERIFY(fourth.hasBaseAddr());
        QCOMPARE(map.findElf(12000), fourth);

        PerfElfMap::ElfInfo fifth(fileInfo, 2000, 500, 3000);
        QVERIFY(!fifth.hasBaseAddr()); // base addr will be set on registering based on first mmap.
        registerElf(&map, fifth);
        fifth.baseAddr = 0;
        QCOMPARE(map.findElf(2200), fifth);

        const PerfElfMap::ElfInfo remainder1(fileInfo, 0, 2000, 0);
        QCOMPARE(map.findElf(100), remainder1);

        const PerfElfMap::ElfInfo remainder2(fileInfo, 2500, 7500, 2500);
        QCOMPARE(map.findElf(3000), remainder2);
    }

    void benchRegisterElfDisjunct()
    {
        QFETCH(uint, numElfMaps);
        const quint64 ADDR_STEP = 1024;
        const quint64 MAX_ADDR = ADDR_STEP * numElfMaps;
        const quint64 LEN = 1024;
        QBENCHMARK {
            PerfElfMap map;
            for (quint64 addr = 0; addr < MAX_ADDR; addr += ADDR_STEP) {
                map.registerElf(addr, LEN, 0, {});
            }
        }
    }

    void benchRegisterElfDisjunct_data()
    {
        QTest::addColumn<uint>("numElfMaps");
        QTest::newRow("10") << 10u;
        QTest::newRow("100") << 100u;
        QTest::newRow("1000") << 1000u;
        QTest::newRow("2000") << 2000u;
    }

    void benchRegisterElfOverlapping()
    {
        QFETCH(uint, numElfMaps);
        const quint64 ADDR_STEP = 1024;
        const quint64 MAX_ADDR = ADDR_STEP * numElfMaps;
        quint64 len = MAX_ADDR;
        QBENCHMARK {
            PerfElfMap map;
            for (quint64 addr = 0; addr < MAX_ADDR; addr += ADDR_STEP, len -= ADDR_STEP) {
                map.registerElf(addr, len, 0, {});
            }
        }
    }

    void benchRegisterElfOverlapping_data()
    {
        benchRegisterElfDisjunct_data();
    }


    void benchRegisterElfExpanding()
    {
        QFETCH(uint, numElfMaps);
        const quint64 ADDR = 0;
        const quint64 LEN_STEP = 1024;
        const quint64 MAX_LEN = LEN_STEP * numElfMaps;
        QBENCHMARK {
            PerfElfMap map;
            for (quint64 len = LEN_STEP; len <= MAX_LEN; len += LEN_STEP) {
                map.registerElf(ADDR, len, 0, {});
            }
        }
    }

    void benchRegisterElfExpanding_data()
    {
        benchRegisterElfDisjunct_data();
    }

    void benchFindElfDisjunct()
    {
        QFETCH(uint, numElfMaps);

        PerfElfMap map;

        const quint64 ADDR_STEP = 1024;
        const quint64 MAX_ADDR = ADDR_STEP * numElfMaps;
        const quint64 LEN = 1024;
        for (quint64 addr = 0; addr < MAX_ADDR; addr += ADDR_STEP) {
            map.registerElf(addr, LEN, 0, {});
        }

        const quint64 ADDR_STEP_FIND = 64;
        QBENCHMARK {
            for (quint64 addr = 0; addr < MAX_ADDR; addr += ADDR_STEP_FIND) {
                auto it = map.findElf(addr);
                Q_UNUSED(it);
            }
        }
    }

    void benchFindElfDisjunct_data()
    {
        benchRegisterElfDisjunct_data();
    }

    void benchFindElfOverlapping()
    {
        QFETCH(uint, numElfMaps);

        PerfElfMap map;

        const quint64 ADDR_STEP = 1024;
        const quint64 MAX_ADDR = ADDR_STEP * numElfMaps;
        quint64 LEN = MAX_ADDR;
        for (quint64 addr = 0; addr < MAX_ADDR; addr += ADDR_STEP, LEN -= ADDR_STEP) {
            map.registerElf(addr, LEN, 0, {});
        }

        const quint64 ADDR_STEP_FIND = 64;
        QBENCHMARK {
            for (quint64 addr = 0; addr < MAX_ADDR; addr += ADDR_STEP_FIND) {
                auto it = map.findElf(addr);
                Q_UNUSED(it);
            }
        }
    }

    void benchFindElfOverlapping_data()
    {
        benchRegisterElfDisjunct_data();
    }

    void benchFindElfExpanding()
    {
        QFETCH(uint, numElfMaps);

        PerfElfMap map;

        const quint64 FIRST_ADDR = 0;
        const quint64 LEN_STEP = 1024;
        const quint64 MAX_LEN = LEN_STEP * numElfMaps;
        for (quint64 len = LEN_STEP; len <= MAX_LEN; len += LEN_STEP) {
            map.registerElf(FIRST_ADDR, len, 0, {});
        }

        const quint64 MAX_ADDR = FIRST_ADDR + MAX_LEN;
        const quint64 ADDR_STEP_FIND = 64;
        QBENCHMARK {
            for (quint64 addr = FIRST_ADDR; addr < MAX_ADDR; addr += ADDR_STEP_FIND) {
                auto it = map.findElf(addr);
                Q_UNUSED(it);
            }
        }
    }

    void benchFindElfExpanding_data()
    {
        benchRegisterElfDisjunct_data();
    }

private:
    QVector<PerfElfMap::ElfInfo> registerElf(PerfElfMap *map, const PerfElfMap::ElfInfo &info)
    {
        QVector<PerfElfMap::ElfInfo> invalidated;
        auto connection = connect(map, &PerfElfMap::aboutToInvalidate,
                                  this, [&](const PerfElfMap::ElfInfo &other) {
                                    invalidated.push_back(other);
                                  });
        map->registerElf(info.addr, info.length, info.pgoff, info.localFile,
                         info.originalFileName, info.originalPath);
        disconnect(connection);
        return invalidated;
    }
};

QTEST_GUILESS_MAIN(TestElfMap)

#include "tst_elfmap.moc"
