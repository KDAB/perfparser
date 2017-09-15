/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd
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
#include <QBuffer>
#include <QSignalSpy>
#include <QDebug>

#include "perfdata.h"
#include "perfunwind.h"

class TestPerfData : public QObject
{
    Q_OBJECT
private slots:
    void testTraceDataHeaderEvent();
};

void TestPerfData::testTraceDataHeaderEvent()
{
    QBuffer output;
    QFile input(":/probe.data.stream");
    QVERIFY(input.open(QIODevice::ReadOnly));
    QVERIFY(output.open(QIODevice::WriteOnly));

    PerfUnwind unwind(&output, QDir::rootPath(), PerfUnwind::defaultDebugInfoPath(), QString(),
                      QString(), true);
    PerfHeader header(&input);
    PerfAttributes attributes;
    PerfData data(&input, &unwind, &header, &attributes);

    QSignalSpy spy(&data, SIGNAL(finished()));
    connect(&header, &PerfHeader::finished, &data, &PerfData::read);
    header.read();
    QCOMPARE(spy.count(), 1);

    unwind.flushEventBuffer();

    const PerfUnwind::Stats stats = unwind.stats();
    QCOMPARE(stats.numSamples, 1u);
    QCOMPARE(stats.numMmaps, 120u);
    QCOMPARE(stats.numRounds, 1u);
    QCOMPARE(stats.numBufferFlushes, 1u);
    QCOMPARE(stats.numTimeViolatingSamples, 0u);
    QCOMPARE(stats.numTimeViolatingMmaps, 0u);
    QCOMPARE(stats.maxBufferSize, 15488u);
    QCOMPARE(stats.maxTime, 13780586522722ull);
}

QTEST_GUILESS_MAIN(TestPerfData)

#include "tst_perfdata.moc"
