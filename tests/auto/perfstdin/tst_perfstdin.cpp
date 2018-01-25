/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd
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

#include "perfstdin.h"

#include <QString>
#include <QtTest>
#include <QTemporaryFile>

#include <cstdio>

class TestPerfstdin : public QObject
{
    Q_OBJECT

private slots:
    void testReadSelf();
};

void TestPerfstdin::testReadSelf()
{
    std::freopen(QCoreApplication::applicationFilePath().toUtf8().constData(), "rb", stdin);
    QTemporaryFile tempfile;
    QVERIFY(tempfile.open());
    PerfStdin device;
    int i = 0;

    auto doRead = [&](){
        QVERIFY(device.bytesAvailable() > 0);
        qint64 r = device.bytesAvailable() + ((++i) % 32) - 16;
        const QByteArray data = device.read(r);
        qint64 pos = 0;
        while (pos < data.length()) {
            qint64 written = tempfile.write(data.data() + pos, data.length() - pos);
            QVERIFY(written >= 0);
            pos += written;
        }
        QCOMPARE(pos, data.length());
    };

    QObject::connect(&device, &QIODevice::readyRead, &tempfile, doRead);

    QObject::connect(&device, &QIODevice::aboutToClose, &tempfile, [&](){
        while (device.bytesAvailable() > 0)
            doRead();
        while (tempfile.bytesToWrite() > 0)
            QVERIFY(tempfile.flush());

        QFile self(QCoreApplication::applicationFilePath());
        QCOMPARE(tempfile.pos(), self.size());

        QVERIFY(tempfile.reset());
        QVERIFY(self.open(QIODevice::ReadOnly));
        char c1, c2;
        while (!self.atEnd()) {
            QVERIFY(self.getChar(&c1));
            QVERIFY(tempfile.getChar(&c2));
            QCOMPARE(c1, c2);
        }
        QVERIFY(self.atEnd());
        QVERIFY(tempfile.atEnd());
        tempfile.close();
    });

    QVERIFY(device.open(QIODevice::ReadOnly));
    QTRY_VERIFY(!tempfile.isOpen());
}

QTEST_MAIN(TestPerfstdin)

#include "tst_perfstdin.moc"
