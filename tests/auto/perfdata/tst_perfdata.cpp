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
#include <QStandardPaths>

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
                        PerfAttributes *attributes, PerfData *data, const QByteArray &expectedVersion)
{
    if (!header->isPipe()) {
        const qint64 filePos = input->pos();

        attributes->read(input, header);

        PerfFeatures features;
        features.read(input, header);

        if (header->hasFeature(PerfHeader::COMPRESSED))
            data->setCompressed(features.compressed());

        PerfTracingData tracingData = features.tracingData();
        if (tracingData.size() > 0)
            QCOMPARE(tracingData.version(), expectedVersion);

        unwind->features(features);
        const auto& attrs = attributes->attributes();
        for (auto it = attrs.begin(), end = attrs.end(); it != end; ++it)
            unwind->attr(PerfRecordAttr(it.value(), {it.key()}));
        const QByteArray &featureArch = features.architecture();
        unwind->setArchitecture(PerfRegisterInfo::archByName(featureArch));
        input->seek(filePos);
    }
}

static void process(PerfUnwind *unwind, QIODevice *input, const QByteArray &expectedVersion)
{
    PerfHeader header(input);
    PerfAttributes attributes;
    PerfData data(unwind, &header, &attributes);
    data.setSource(input);

    QSignalSpy spy(&data, &PerfData::finished);
    QObject::connect(&header, &PerfHeader::finished, &data, [&](){
        setupUnwind(unwind, &header, input, &attributes, &data, expectedVersion);
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
    PerfUnwind unwind(&output, QStringLiteral(":/"), QString(), QString(), QString(), stats);
    if (!stats) {
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(
                                 QStringLiteral("Could not find ELF file for "
                                                "/home/ulf/dev/untitled1-Qt_5_9_1_gcc_64-Profile/untitled1. "
                                                "This can break stack unwinding and lead to missing symbols."))));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(
                                 QStringLiteral("Could not find ELF file for "
                                                "/lib/x86_64-linux-gnu/ld-2.24.so. "
                                                "This can break stack unwinding and lead to missing symbols."))));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(
                                 QStringLiteral("Could not find ELF file for "
                                                "/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.22. "
                                                "This can break stack unwinding and lead to missing symbols."))));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(
                                 QStringLiteral("Could not find ELF file for "
                                                "/lib/x86_64-linux-gnu/libm-2.24.so. "
                                                "This can break stack unwinding and lead to missing symbols."))));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(
                                 QStringLiteral("Could not find ELF file for "
                                                "/lib/x86_64-linux-gnu/libgcc_s.so.1. "
                                                "This can break stack unwinding and lead to missing symbols."))));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(
                                 QStringLiteral("Could not find ELF file for "
                                                "/lib/x86_64-linux-gnu/libc-2.24.so. "
                                                "This can break stack unwinding and lead to missing symbols."))));
    }
    process(&unwind, &input, QByteArray("0.5"));

    if (stats) {
        const PerfUnwind::Stats stats = unwind.stats();
        QCOMPARE(stats.numSamples, 1u);
        QCOMPARE(stats.numMmaps, 120u);
        QCOMPARE(stats.numRounds, 2u);
        QCOMPARE(stats.numBufferFlushes, flushes);
        QCOMPARE(stats.numTimeViolatingSamples, 0u);
        QCOMPARE(stats.numTimeViolatingMmaps, 0u);
        QCOMPARE(stats.maxBufferSize, 15608u);
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
    QString file(QStringLiteral(":/contentsize.data"));

    QBuffer output;
    QFile input(file);

    QVERIFY(input.open(QIODevice::ReadOnly));
    QVERIFY(output.open(QIODevice::WriteOnly));

    // Don't try to load any system files. They are not the same as the ones we use to report.
    PerfUnwind unwind(&output, QStringLiteral(":/"), QString(), QString(), QString(), true);
    process(&unwind, &input, QByteArray("0.5"));

    QCOMPARE(unwind.stats().numSamples, 69u);
}

Q_DECL_UNUSED static void compressFile(const QString& input, const QString& output = QString())
{
    QVERIFY(!input.isEmpty() && QFile::exists(input));

    if (output.isEmpty()) {
        compressFile(input, input + QLatin1String(".zlib"));
        return;
    }

    QFile raw(input);
    QVERIFY(raw.open(QIODevice::ReadOnly));

    QFile compressed(output);
    QVERIFY(compressed.open(QIODevice::WriteOnly));

    compressed.write(qCompress(raw.readAll()));
}

static void uncompressFile(const QString& input, const QString& output = QString())
{
    QVERIFY(!input.isEmpty() && QFile::exists(input));

    if (output.isEmpty()) {
        auto suffix = QLatin1String(".zlib");
        QVERIFY(input.endsWith(suffix));
        uncompressFile(input, input.chopped(suffix.size()));
        return;
    }

    QFile compressed(input);
    QVERIFY(compressed.open(QIODevice::ReadOnly));

    QFile raw(output);
    QVERIFY(raw.open(QIODevice::WriteOnly));

    raw.write(qUncompress(compressed.readAll()));
}

void TestPerfData::testFiles_data()
{
    QTest::addColumn<QString>("dataFile");

    // to add a new compressed binary, you'd run this test once with a line like the following:
    // compressFile(QFINDTESTDATA("vector_static_clang/vector_static_clang_v8.0.1"));

    // uncompress binaries to let unwinding work
    uncompressFile(QFINDTESTDATA("vector_static_clang/vector_static_clang_v8.0.1.zlib"));
    uncompressFile(QFINDTESTDATA("vector_static_gcc/vector_static_gcc_v9.1.0.zlib"));
    uncompressFile(QFINDTESTDATA("fork_static_gcc/fork.zlib"));
    uncompressFile(QFINDTESTDATA("parallel_static_gcc/parallel_static_gcc.zlib"));

    const auto files = {
        "vector_static_clang/perf.data",
        "vector_static_gcc/perf.data",
        "vector_static_gcc/perf.lbr.data",
        "vector_static_gcc/perf.data.zstd",
        "fork_static_gcc/perf.data.zstd",
        "parallel_static_gcc/perf.data.zstd",
    };
    for (auto file : files)
        QTest::addRow("%s", file) << file;
}

void TestPerfData::testFiles()
{
    QFETCH(QString, dataFile);
#ifndef HAVE_ZSTD
    if (dataFile.contains(QStringLiteral("zstd")))
        QSKIP("zstd support disabled, skipping test");
#endif

    const auto perfDataFileCompressed = QFINDTESTDATA(dataFile + QLatin1String(".zlib"));
    QVERIFY(!perfDataFileCompressed.isEmpty() && QFile::exists(perfDataFileCompressed));
    uncompressFile(perfDataFileCompressed);

    const auto perfDataFile = QFINDTESTDATA(dataFile);
    QVERIFY(!perfDataFile.isEmpty() && QFile::exists(perfDataFile));
    const auto expectedOutputFileCompressed = QString(perfDataFile + QLatin1String(".expected.txt.zlib"));
    const auto expectedOutputFileUncompressed = QString(perfDataFile + QLatin1String(".expected.txt"));
    const auto actualOutputFile = QString(perfDataFile + QLatin1String(".actual.txt"));

    QBuffer output;
    QVERIFY(output.open(QIODevice::WriteOnly));

    // Don't try to load any system files. They are not the same as the ones we use to report.
    PerfUnwind unwind(&output, QStringLiteral(":/"), QString(), QString(), QFileInfo(perfDataFile).absolutePath());
    {
        QFile input(perfDataFile);
        QVERIFY(input.open(QIODevice::ReadOnly));
        // don't try to parse kallsyms here, it's not the main point and it wouldn't be portable without the mapping file
        // from where we recorded the data. these files are usually large, and we don't want to bloat the repo too much
        if (QLatin1String(QTest::currentDataTag()) != QLatin1String("fork_static_gcc/perf.data.zstd")) {
            QTest::ignoreMessage(QtWarningMsg,
                                 QRegularExpression(QStringLiteral(
                                     "Failed to parse kernel symbol mapping file \".+\": Mapping is empty")));
        }
        unwind.setKallsymsPath(QProcess::nullDevice());

        auto version = QByteArray("0.5");
        if (dataFile == QLatin1String("parallel_static_gcc/perf.data.zstd"))
            version = "0.6";
        process(&unwind, &input, version);
    }

    output.close();
    output.open(QIODevice::ReadOnly);

    QString actualText;
    {
        QTextStream stream(&actualText);
        PerfParserTestClient client;
        client.extractTrace(&output);
        client.convertToText(stream);
        stream.flush();

        // some older platforms produce strange type names for complex doubles...
        // similarly, older systems failed to demangle some symbols
        // always use the new form as the canonical form in our expected files
        // and replace the actual text in case we run on an older system
        const std::pair<const char*, const char*> replacements[] = {
            {"doublecomplex ", "double _Complex"},
            {"_ZSt3cosIiEN9__gnu_cxx11__enable_ifIXsr12__is_integerIT_EE7__valueEdE6__typeES2_",
             "__gnu_cxx::__enable_if<__is_integer<int>::__value, double>::__type std::cos<int>(int)"},
            {"_ZSt3sinIiEN9__gnu_cxx11__enable_ifIXsr12__is_integerIT_EE7__valueEdE6__typeES2_",
             "__gnu_cxx::__enable_if<__is_integer<int>::__value, double>::__type std::sin<int>(int)"},
            {"_ZSt14__relocate_a_1IddENSt9enable_ifIXsr3std24__is_bitwise_relocatableIT_EE5valueEPS1_E4typeES2_S2_S2_"
             "RSaIT0_E",
             "std::enable_if<std::__is_bitwise_relocatable<double>::value, double*>::type std::__relocate_a_1<double, "
             "double>(double*, double*, double*, std::allocator<double>&)"}};
        for (const auto& replacement : replacements) {
            actualText.replace(QLatin1String(replacement.first), QLatin1String(replacement.second));
        }

        QFile actual(actualOutputFile);
        QVERIFY(actual.open(QIODevice::WriteOnly | QIODevice::Text));
        actual.write(actualText.toUtf8());
    }

    QString expectedText;
    {
        QFile expected(expectedOutputFileCompressed);
        QVERIFY(expected.open(QIODevice::ReadOnly));
        expectedText = QString::fromUtf8(qUncompress(expected.readAll()));
    }

    if (actualText != expectedText) {
        compressFile(actualOutputFile);

        const auto diff = QStandardPaths::findExecutable(QStringLiteral("diff"));
        if (!diff.isEmpty()) {
            {
                QFile expectedUncompressed(expectedOutputFileUncompressed);
                QVERIFY(expectedUncompressed.open(QIODevice::WriteOnly | QIODevice::Text));
                expectedUncompressed.write(expectedText.toUtf8());
            }
            QProcess::execute(diff, {QStringLiteral("-u"), expectedOutputFileUncompressed, actualOutputFile});
        }
    }
    QCOMPARE(actualText, expectedText);
}

QTEST_GUILESS_MAIN(TestPerfData)

#include "tst_perfdata.moc"
