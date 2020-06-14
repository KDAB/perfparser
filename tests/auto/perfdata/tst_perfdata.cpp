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

#include "perfdata.h"
#include "perfparsertestclient.h"
#include "perfunwind.h"

#include <QBuffer>
#include <QDebug>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QtEndian>
#include <QProcess>
#include <QRegularExpression>

class TestPerfData : public QObject
{
    Q_OBJECT
private slots:
    void testTracingData_data();
    void testTracingData();
    void testContentSize();
    void testFiles_data();
    void testFiles();
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
        if (tracingData.size() > 0)
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
    PerfData data(unwind, &header, &attributes);
    data.setSource(input);

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
    QTest::newRow("stream stats") << ":/probe.data.stream" << 1u << 13780586573357ull << true;
    QTest::newRow("file stats") << ":/probe.data.file" << 1u << 13732862274100ull << true;
    QTest::newRow("stream data") << ":/probe.data.stream" << 2u << 13780586522722ull << false;
    QTest::newRow("file data") << ":/probe.data.file" << 3u << 13732862219876ull << false;
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
                             QRegularExpression(QRegularExpression::escape("Could not find ELF file for "
                             "/home/ulf/dev/untitled1-Qt_5_9_1_gcc_64-Profile/untitled1. "
                             "This can break stack unwinding and lead to missing symbols.")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape("Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/ld-2.24.so. "
                             "This can break stack unwinding and lead to missing symbols.")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape("Could not find ELF file for "
                             "/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.22. "
                             "This can break stack unwinding and lead to missing symbols.")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape("Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/libm-2.24.so. "
                             "This can break stack unwinding and lead to missing symbols.")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape("Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/libgcc_s.so.1. "
                             "This can break stack unwinding and lead to missing symbols.")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape("Could not find ELF file for "
                             "/lib/x86_64-linux-gnu/libc-2.24.so. "
                             "This can break stack unwinding and lead to missing symbols.")));
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
        QCOMPARE(stats.maxBufferSize, 15584u);
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
        QCOMPARE(sample.values.size(), 1);
        const PerfParserTestClient::AttributeEvent attribute
                = client.attribute(sample.values[0].first);
        QCOMPARE(attribute.type, 2u);
        QVERIFY(attribute.config <= quint64(std::numeric_limits<qint32>::max()));
        const PerfParserTestClient::TracePointFormatEvent format
                = client.tracePointFormat(static_cast<qint32>(attribute.config));
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

void TestPerfData::testFiles_data()
{
    QTest::addColumn<QString>("dirName");

    for (auto dir : {"vector_static_clang", "vector_static_gcc"})
        QTest::addRow("%s", dir) << dir;
}

void TestPerfData::testFiles()
{
    QFETCH(QString, dirName);

    const auto dir = QFINDTESTDATA(dirName);
    QVERIFY(!dir.isEmpty() && QFile::exists(dir));
    const auto perfDataFile = dir + "/perf.data";
    const auto expectedOutputFile = dir + "/expected.txt";
    const auto actualOutputFile = dir + "/actual.txt";

    QBuffer output;
    QVERIFY(output.open(QIODevice::WriteOnly));

    // Don't try to load any system files. They are not the same as the ones we use to report.
    PerfUnwind unwind(&output, ":/", QString(), QString(), dir);
    {
        QFile input(perfDataFile);
        QVERIFY(input.open(QIODevice::ReadOnly));
        // don't try to parse kallsyms here, it's not the main point and it wouldn't be portable without the mapping file
        // from where we recorded the data. these files are usually large, and we don't want to bloat the repo too much
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Failed to parse kernel symbol mapping file \".+\": Mapping is empty"));
        unwind.setKallsymsPath(QProcess::nullDevice());
        process(&unwind, &input);
    }

    output.close();
    output.open(QIODevice::ReadOnly);

    QString actualText;
    {
        QTextStream stream(&actualText);
        PerfParserTestClient client;
        client.extractTrace(&output);
        client.convertToText(stream);

        QFile actual(actualOutputFile);
        QVERIFY(actual.open(QIODevice::WriteOnly | QIODevice::Text));
        actual.write(actualText.toUtf8());
    }

    QString expectedText;
    {
        QFile expected(expectedOutputFile);
        QVERIFY(expected.open(QIODevice::ReadOnly | QIODevice::Text));
        expectedText = QString::fromUtf8(expected.readAll());
    }

    QCOMPARE(actualText, expectedText);
}

QTEST_GUILESS_MAIN(TestPerfData)

#include "tst_perfdata.moc"
