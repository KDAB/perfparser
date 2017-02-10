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

#include "perfkallsyms.h"

class TestKallsyms : public QObject
{
    Q_OBJECT
private slots:
    void testResolve_data()
    {
        QTest::addColumn<QByteArray>("kallsymsContents");
        QTest::addColumn<quint64>("address");
        QTest::addColumn<quint64>("expectedAddress");
        QTest::addColumn<QByteArray>("expectedSymbol");
        QTest::addColumn<QByteArray>("expectedModule");

        {
            const QByteArray kallsyms =
                "0000000000000000 A irq_stack_union\n"
                "0000000000000000 A __per_cpu_start\n"
                "ffffffff810002b8 T _stext\n"
                "ffffffff81001000 T hypercall_page\n"
                "ffffffff81001000 t xen_hypercall_set_trap_table\n"
                "ffffffff81001020 t xen_hypercall_mmu_update\n"
                "ffffffff81001040 t xen_hypercall_set_gdt\n"
                "ffffffffa0000e80 T serio_interrupt\t[serio]\n"
                "ffffffffa0000de0 T serio_unregister_driver\t[serio]\n";

            QTest::newRow("__per_cpu_start:0") << kallsyms << 0x0ull
                 << 0x0ull << QByteArrayLiteral("__per_cpu_start") << QByteArray();
            QTest::newRow("_stext:0") << kallsyms << 0xffffffff810002b8ull
                << 0xffffffff810002b8ull << QByteArrayLiteral("_stext") << QByteArray();
            QTest::newRow("_stext:2") << kallsyms << (0xffffffff810002b8ll + 0x2ull)
                << 0xffffffff810002b8ll << QByteArrayLiteral("_stext") << QByteArray();
            QTest::newRow("xen_hypercall_set_gdt:0") << kallsyms << 0xffffffff81001040ull
                << 0xffffffff81001040ull << QByteArrayLiteral("xen_hypercall_set_gdt") << QByteArray();
            QTest::newRow("xen_hypercall_set_gdt:256") << kallsyms << (0xffffffff81001040ull + 0x100ull)
                << 0xffffffff81001040ull << QByteArrayLiteral("xen_hypercall_set_gdt") << QByteArray();
            QTest::newRow("xen_hypercall_set_gdt:256") << kallsyms << (0xffffffff81001040ull + 0x100ull)
                << 0xffffffff81001040ull << QByteArrayLiteral("xen_hypercall_set_gdt") << QByteArray();
            QTest::newRow("serio_interrupt:0") << kallsyms << 0xffffffffa0000e80ull
                << 0xffffffffa0000e80ull << QByteArrayLiteral("serio_interrupt") << QByteArrayLiteral("[serio]");
        }
        {
            const auto kallsyms = QByteArrayLiteral("0000000000000000 A irq_stack_union");
            QTest::newRow("zeros-only") << kallsyms << 0x0ull
                << 0x0ull << QByteArrayLiteral("irq_stack_union") << QByteArray();
            QTest::newRow("zeros-only") << kallsyms << std::numeric_limits<quint64>::max()
                << 0x0ull
                << QByteArrayLiteral("irq_stack_union") << QByteArray();
        }
    }

    void testResolve()
    {
        QFETCH(QByteArray, kallsymsContents);
        QFETCH(quint64, address);
        QFETCH(quint64, expectedAddress);
        QFETCH(QByteArray, expectedSymbol);
        QFETCH(QByteArray, expectedModule);

        QTemporaryFile file;
        QVERIFY(file.open());
        file.write(kallsymsContents);
        file.flush();

        PerfKallsyms kallsyms(file.fileName());

        const auto entry = kallsyms.findEntry(address);
        QCOMPARE(entry.address, expectedAddress);
        QCOMPARE(entry.symbol, expectedSymbol);
        QCOMPARE(entry.module, expectedModule);
    }

    void testProc()
    {
        const auto path = QStringLiteral("/proc/kallsyms");
        if (!QFile::exists(path))
            QSKIP("/proc/kallsysms not available");

        PerfKallsyms kallsyms(path);

        // just check that we find any entry
        const auto addr = std::numeric_limits<quint64>::max();
        const auto entry = kallsyms.findEntry(addr);
        QVERIFY(!entry.symbol.isEmpty());
    }

    void benchmarkProc()
    {
        const auto path = QStringLiteral("/proc/kallsyms");
        if (!QFile::exists(path))
            QSKIP("/proc/kallsysms not available");

        QBENCHMARK {
            PerfKallsyms kallsyms(path);
            Q_UNUSED(kallsyms);
        }
    }
};

QTEST_GUILESS_MAIN(TestKallsyms)

#include "tst_kallsyms.moc"
