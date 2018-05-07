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
#include <QtEndian>

#include "perfdata.h"
#include "perfunwind.h"
#include "perfparsertestclient.h"

class TestPerfData : public QObject
{
    Q_OBJECT
private slots:
    void testTracingData_data();
    void testTracingData();
    void testContentSize();
};

static void setupUnwind(PerfUnwind *unwind, PerfHeader *header, QIODevice *input,
                        PerfAttributes *attributes)
{
    if (!header->isPipe()) {
        const qint64 filePos = input->pos();

        attributes->read(input, header);

        PerfFeatures features;
        features.read(input, header);

        PerfTracingData tracingData = features.tracingData();
        QVERIFY(tracingData.size() > 0);
        QCOMPARE(tracingData.version(), QByteArray("0.5"));

        unwind->features(features);
        const auto& attrs = attributes->attributes();
        for (auto it = attrs.begin(), end = attrs.end(); it != end; ++it)
            unwind->attr(PerfRecordAttr(it.value(), {it.key()}));
        const QByteArray &featureArch = features.architecture();
        unwind->setArchitecture(PerfRegisterInfo::archByName(featureArch));
        input->seek(filePos);
    }
}

static void process(PerfUnwind *unwind, QIODevice *input)
{
    PerfHeader header(input);
    PerfAttributes attributes;
    PerfData data(input, unwind, &header, &attributes);

    QSignalSpy spy(&data, SIGNAL(finished()));
    QObject::connect(&header, &PerfHeader::finished, &data, [&](){
        setupUnwind(unwind, &header, input, &attributes);
        data.read();
    });

    QObject::connect(&data, &PerfData::error, []() {
        QFAIL("PerfData reported an error");
    });

    header.read();
    QCOMPARE(spy.count(), 1);

    unwind->finalize();
}

void TestPerfData::testTracingData_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<uint>("flushes");
    QTest::addColumn<quint64>("maxTime");
    QTest::addColumn<bool>("stats");
    QTest::addRow("stream stats") << ":/probe.data.stream" << 2u << 13780586522722ull << true;
    QTest::addRow("file stats") << ":/probe.data.file" << 3u << 13732862219876ull << true;
    QTest::addRow("stream data") << ":/probe.data.stream" << 2u << 13780586522722ull << false;
    QTest::addRow("file data") << ":/probe.data.file" << 3u << 13732862219876ull << false;
}

void TestPerfData::testTracingData()
{
    QFETCH(QString, file);
    QFETCH(uint, flushes);
    QFETCH(quint64, maxTime);
    QFETCH(bool, stats);

    QBuffer output;
    QFile input(file);

    QVERIFY(input.open(QIODevice::ReadOnly));
    QVERIFY(output.open(QIODevice::WriteOnly));

    // Don't try to load any system files. They are not the same as the ones we use to report.
    PerfUnwind unwind(&output, ":/", QString(), QString(), QString(), stats);
    if (!stats) {
        QTest::ignoreMessage(QtWarningMsg,
                             "PerfUnwind::ErrorCode(MissingElfFile): Could not find ELF file for "
                             "/home/ulf/dev/untitled1-Qt_5_9_1_gcc_64-Profile/untitled1. "
                             "This can break stack unwinding and lead to missing symbols.");
        QTest::ignoreMessage(QtWarningMsg,
                             "PerfUnwind::ErrorCode(MissingElfFile): Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/ld-2.24.so. "
                             "This can break stack unwinding and lead to missing symbols.");
        QTest::ignoreMessage(QtWarningMsg,
                             "PerfUnwind::ErrorCode(MissingElfFile): Could not find ELF file for "
                             "/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.22. "
                             "This can break stack unwinding and lead to missing symbols.");
        QTest::ignoreMessage(QtWarningMsg,
                             "PerfUnwind::ErrorCode(MissingElfFile): Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/libm-2.24.so. "
                             "This can break stack unwinding and lead to missing symbols.");
        QTest::ignoreMessage(QtWarningMsg,
                             "PerfUnwind::ErrorCode(MissingElfFile): Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/libgcc_s.so.1. "
                             "This can break stack unwinding and lead to missing symbols.");
        QTest::ignoreMessage(QtWarningMsg,
                             "PerfUnwind::ErrorCode(MissingElfFile): Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/libc-2.24.so. "
                             "This can break stack unwinding and lead to missing symbols.");
    }
    process(&unwind, &input);

    if (stats) {
        const PerfUnwind::Stats stats = unwind.stats();
        QCOMPARE(stats.numSamples, 1u);
        QCOMPARE(stats.numMmaps, 120u);
        QCOMPARE(stats.numRounds, 2u);
        QCOMPARE(stats.numBufferFlushes, flushes);
        QCOMPARE(stats.numTimeViolatingSamples, 0u);
        QCOMPARE(stats.numTimeViolatingMmaps, 0u);
        QCOMPARE(stats.maxBufferSize, 15488u);
        QCOMPARE(stats.maxTime, maxTime);
        return;
    }

    output.close();
    output.open(QIODevice::ReadOnly);

    PerfParserTestClient client;
    client.extractTrace(&output);

    const QVector<PerfParserTestClient::SampleEvent> samples = client.samples();
    QVERIFY(samples.length() > 0);
    for (const PerfParserTestClient::SampleEvent &sample : samples) {
        const PerfParserTestClient::AttributeEvent attribute
                = client.attribute(sample.attributeId);
        QCOMPARE(attribute.type, 2u);
        const PerfParserTestClient::TracePointFormatEvent format
                = client.tracePointFormat(attribute.config);
        QCOMPARE(client.string(format.system), QByteArray("probe_untitled1"));
        QCOMPARE(client.string(format.name), QByteArray("main"));
        QCOMPARE(format.flags, 0u);

        QCOMPARE(sample.tracePointData.size(), 1);
        auto it = sample.tracePointData.constBegin();
        QCOMPARE(client.string(it.key()), QByteArray("__probe_ip"));
        QCOMPARE(it.value().type(), QVariant::ULongLong);
    }
}

void TestPerfData::testContentSize()
{
    QString file(":/contentsize.data");

    QBuffer output;
    QFile input(file);

    QVERIFY(input.open(QIODevice::ReadOnly));
    QVERIFY(output.open(QIODevice::WriteOnly));

    // Don't try to load any system files. They are not the same as the ones we use to report.
    PerfUnwind unwind(&output, ":/", QString(), QString(), QString(), true);
    process(&unwind, &input);

    QCOMPARE(unwind.stats().numSamples, 69u);
}

QTEST_GUILESS_MAIN(TestPerfData)

#include "tst_perfdata.moc"
