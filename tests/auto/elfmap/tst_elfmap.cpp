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

#include "perfelfmap.h"

class TestElfMap : public QObject
{
    Q_OBJECT
private slots:
    void testNoOverlap()
    {
        PerfElfMap map;
        QVERIFY(map.isEmpty());

        QVERIFY(!map.registerElf(100, 10, 0, {}));
        QVERIFY(!map.isEmpty());

        QCOMPARE(map.constBegin().key(), 100ull);
        QCOMPARE(map.constBegin()->length, 10ull);
        QCOMPARE(map.constBegin()->timeAdded, 0ull);

        QCOMPARE(map.findElf(99, 0), map.constEnd());
        QCOMPARE(map.findElf(100, 0), map.constBegin());
        QCOMPARE(map.findElf(109, 0), map.constBegin());
        QCOMPARE(map.findElf(110, 0), map.constEnd());
        QCOMPARE(map.findElf(105, 1), map.constBegin());

        QVERIFY(!map.registerElf(0, 10, 1, {}));

        QCOMPARE(map.constBegin().key(), 0ull);
        QCOMPARE(map.constBegin()->length, 10ull);
        QCOMPARE(map.constBegin()->timeAdded, 1ull);

        auto first = map.constBegin();
        auto second = first + 1;

        QCOMPARE(map.findElf(0, 0), map.constEnd());
        QCOMPARE(map.findElf(0, 1), first);
        QCOMPARE(map.findElf(5, 1), first);
        QCOMPARE(map.findElf(9, 1), first);
        QCOMPARE(map.findElf(10, 0), map.constEnd());
        QCOMPARE(map.findElf(5, 2), first);

        QCOMPARE(map.findElf(99, 0), map.constEnd());
        QCOMPARE(map.findElf(100, 0), second);
        QCOMPARE(map.findElf(109, 0), second);
        QCOMPARE(map.findElf(110, 0), map.constEnd());
        QCOMPARE(map.findElf(105, 1), second);
    }

    void testOverwrite()
    {
        QFETCH(bool, reversed);

        PerfElfMap map;
        if (!reversed) {
            QVERIFY(!map.registerElf(100, 20, 0, {}));
            QVERIFY(map.registerElf(100, 20, 1, {}));
        } else {
            QVERIFY(!map.registerElf(100, 20, 1, {}));
            QVERIFY(map.registerElf(100, 20, 0, {}));
        }

        auto first = map.findElf(105, 0);
        QCOMPARE(first.key(), 100ull);
        QCOMPARE(first->length, 20ull);
        QCOMPARE(first->timeAdded, 0ull);
        QCOMPARE(first->timeOverwritten, 1ull);

        auto second = map.findElf(105, 1);
        QCOMPARE(second.key(), 100ull);
        QCOMPARE(second->length, 20ull);
        QCOMPARE(second->timeAdded, 1ull);
        QCOMPARE(second->timeOverwritten, std::numeric_limits<quint64>::max());

        QCOMPARE(map.findElf(105, 2), second);
    }

    void testOverwrite_data()
    {
        QTest::addColumn<bool>("reversed");

        QTest::newRow("normal") << false;
        QTest::newRow("reversed") << true;
    }
};

QTEST_MAIN(TestElfMap)

#include "tst_elfmap.moc"
