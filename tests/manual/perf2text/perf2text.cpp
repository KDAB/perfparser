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

    QTextStream out(stdout);

    PerfParserTestClient client;
    client.extractTrace(&process);
    client.convertToText(out);

    return 0;
}
