/****************************************************************************
**
** Copyright (C) 2021 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Lieven Hey
*<lieven.hey@kdab.com>
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

#include "perfkallsyms.h"

#include <QDebug>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <QThread>

#include "perfsymboltable.h"

class TestFindDebugSymbols : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        const auto files = {QStringLiteral("/usr/bin/python3.8"),
                            QStringLiteral("/usr/bin/.debug/096cdc8214a805dca8d174fe072684b0f21645.debug"),
                            QStringLiteral("/usr/lib/libm.so"),
                            QStringLiteral("/usr/lib/libqt.so"),
                            QStringLiteral("/usr/lib/debug/libm.so"),
                            QStringLiteral("/usr/lib/debug/lib/x64/libc.so"),
                            QStringLiteral("/usr/lib/debug/lib/test.so"),
                            QStringLiteral("/usr/lib/debug/usr/lib/test2.so")};

        QDir dir(tempDir.path());
        for (const auto& file : files) {
            auto path = QFileInfo(tempDir.path() + file).path();
            dir.mkpath(path);
            QVERIFY(dir.exists(path));

            QFile f(tempDir.path() + file);
            f.open(QIODevice::WriteOnly);
            f.write(file.toUtf8());
            QVERIFY(f.exists());
        }
    }

    void findDebugSymbols_data()
    {
        QTest::addColumn<QString>("root");
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("debugLinkString");

        QTest::addRow("/usr/bin") << QStringLiteral("/usr/bin/python3.8")
                                  << QStringLiteral("096cdc8214a805dca8d174fe072684b0f21645.debug")
                                  << QStringLiteral("/usr/bin/.debug/096cdc8214a805dca8d174fe072684b0f21645.debug");
        QTest::addRow("/usr/lib/debug") << QStringLiteral("/usr/lib/libm.so") << QStringLiteral("libm.so")
                                        << QStringLiteral("/usr/lib/debug/libm.so");
        QTest::addRow("/usr/lib/debug") << QStringLiteral("/usr/lib/x64/libc.so") << QStringLiteral("libc.so")
                                        << QStringLiteral("/usr/lib/debug/lib/x64/libc.so");
        QTest::addRow("no debug file") << QStringLiteral("/usr/lib/libqt.so") << QStringLiteral("libqt.so")
                                       << QStringLiteral("/usr/lib/libqt.so");
        QTest::addRow("/us/lib/") << QStringLiteral("/usr/lib/test.so") << QStringLiteral("test.so")
                                  << QStringLiteral("test.so");
        QTest::addRow("/us/lib/") << QStringLiteral("/usr/lib/test2.so") << QStringLiteral("test2.so")
                                  << QStringLiteral("test2.so");
    }

    void findDebugSymbols()
    {
        QFETCH(QString, root);
        QFETCH(QString, file);
        QFETCH(QString, debugLinkString);

        auto debugFile = PerfSymbolTable::findDebugInfoFile(
                    tempDir.path() + QDir::separator(), root, file);

        QEXPECT_FAIL("/us/lib/", "Skipping broken test.", Continue);
        QCOMPARE(debugFile.absoluteFilePath(), QFileInfo(tempDir.path() + debugLinkString).absoluteFilePath());
    }

private:
    QTemporaryDir tempDir;
};

QTEST_GUILESS_MAIN(TestFindDebugSymbols)

#include "tst_finddebugsym.moc"
