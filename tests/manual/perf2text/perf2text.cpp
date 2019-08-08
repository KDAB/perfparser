/****************************************************************************
**
** Copyright (C) 2019 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Milian Wolff <milian.wolff@kdab.com>
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

#include <QProcess>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include "perfparsertestclient.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const auto perfparser = QStandardPaths::findExecutable(QStringLiteral("perfparser"), {app.applicationDirPath()});
    if (perfparser.isEmpty()) {
        qWarning() << "failed to find perfparser executable";
        return 1;
    }

    auto args = app.arguments();
    args.removeFirst();

    QProcess process;
    PerfParserTestClient client;
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    QObject::connect(&process, &QProcess::errorOccurred, &app, [&process](QProcess::ProcessError error) {
        qWarning() << "perfparser process error:" << error << process.errorString();
    });
    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &app, [&process](int exitCode, QProcess::ExitStatus status) {
        if (exitCode != 0 || status != QProcess::NormalExit)
            qWarning() << "perfparser process finished abnormally:" << exitCode << status << process.errorString();
    });
    process.start(perfparser, args);
    if (!process.waitForStarted() || !process.waitForFinished())
        return 1;

    client.extractTrace(&process);

    QTextStream out(stdout);
    for (const auto &sample : client.samples()) {
        out << client.string(client.command(sample.pid).name) << '\t'
            << sample.pid << '\t' << sample.tid << '\t'
            << sample.time / 1000000000 << '.' << qSetFieldWidth(9) << qSetPadChar(QLatin1Char('0'))
            << sample.time % 1000000000 << qSetFieldWidth(0) << qSetPadChar(QLatin1Char(' ')) << '\n';
        for (const auto &value : sample.values) {
            const auto attribute = client.attribute(value.first);
            const auto cost = attribute.usesFrequency ? value.second : attribute.frequencyOrPeriod;
            out << '\t' << client.string(attribute.name) << ": ";
            if (attribute.type == 2) {
                const auto format = client.tracePointFormat(static_cast<qint32>(attribute.config));
                out << client.string(format.system) << ' ' << client.string(format.name) << ' ' << hex << format.flags << dec << '\n';
                for (auto it = sample.tracePointData.begin(); it != sample.tracePointData.end(); ++it) {
                    out << "\t\t" << client.string(it.key()) << '=' << it.value().toString() << '\n';
                }
            } else {
                out << cost << '\n';
            }
        }
        out << '\n';
        auto printFrame = [&out, &client](qint32 locationId) -> qint32 {
            const auto location = client.location(locationId);
            out << '\t' << hex << location.address << dec;
            const auto symbol = client.symbol(locationId);
            if (location.file != -1)
                out << '\t' << client.string(location.file) << ':' << location.line << ':' << location.column;
            if (symbol.path != -1)
                out << '\t' << client.string(symbol.name) << ' ' << client.string(symbol.binary) << ' ' << client.string(symbol.path) << ' ' << (symbol.isKernel ? "[kernel]" : "");
            out << '\n';
            return location.parentLocationId;
        };
        for (const auto &frame : sample.frames) {
            auto locationId = printFrame(frame);
            while (locationId != -1)
                locationId = printFrame(locationId);
        }
        out << '\n';
    }

    return 0;
}
