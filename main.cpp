/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd
** All rights reserved.
** For any questions to The Qt Company, please use contact form at http://www.qt.io/contact-us
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

#include "perfheader.h"
#include "perfattributes.h"
#include "perffeatures.h"
#include "perfdata.h"
#include "perfunwind.h"
#include "perfregisterinfo.h"
#include "perfstdin.h"

#include <QFile>
#include <QDebug>
#include <QtEndian>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QPointer>
#include <QAbstractSocket>
#include <QTcpSocket>
#include <limits>

enum ErrorCodes {
    NoError,
    TcpSocketError,
    CannotOpen,
    BadMagic,
    HeaderError,
    DataError,
    MissingData,
};

class PerfTcpSocket : public QTcpSocket {
    Q_OBJECT
public:
    PerfTcpSocket(QCoreApplication *app);

public slots:
    void readingFinished();
    void processError(QAbstractSocket::SocketError error);

private:
    bool reading;
};

int main(int argc, char *argv[])
{
    int exitCode = -1;

    QCoreApplication app(argc, argv);
    app.setApplicationName(QLatin1String("perfparser"));
    app.setApplicationVersion(QLatin1String("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QLatin1String("Perf data parser and unwinder."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption input(QLatin1String("input"),
                             QCoreApplication::translate(
                                 "main", "Read perf data from <file> instead of stdin."),
                             QLatin1String("file"));
    parser.addOption(input);

    QCommandLineOption host(QLatin1String("host"),
                            QCoreApplication::translate(
                                "main", "Read perf data from remote <host> instead of stdin."),
                            QLatin1String("host"));
    parser.addOption(host);

    QCommandLineOption port(QLatin1String("port"),
                            QCoreApplication::translate(
                              "main", "When reading from remote host, connect to <port> (default: "
                                      "9327)."),
                            QLatin1String("port"),
                            QLatin1String("9327"));
    parser.addOption(port);

    QCommandLineOption output(QLatin1String("output"),
                              QCoreApplication::translate(
                                  "main", "Write b2qt data to <file> instead of stdout."),
                              QLatin1String("file"));
    parser.addOption(output);

    QCommandLineOption sysroot(QLatin1String("sysroot"),
                               QCoreApplication::translate(
                                   "main", "Look for system libraries in <path> (default: /)."),
                               QLatin1String("path"),
                               QLatin1String("/"));
    parser.addOption(sysroot);

    QCommandLineOption debug(QLatin1String("debug"),
                             QCoreApplication::translate(
                                 "main",
                                 "Look for debug information in <path>. "
                                 "You can specify multiple paths separated by ':'. "
                                 "The default is: <sysroot>/usr/lib/debug."),
                             QLatin1String("path"),
                             QLatin1String("/usr/lib/debug"));
    parser.addOption(debug);

    QCommandLineOption extra(QLatin1String("extra"),
                             QCoreApplication::translate(
                                 "main", "Look for additional libraries in <path> (default: .)."),
                             QLatin1String("path"),
                             QLatin1String("."));
    parser.addOption(extra);

    QCommandLineOption appPath(QLatin1String("app"),
                               QCoreApplication::translate(
                                   "main", "Look for application binary in <path> (default: .)."),
                               QLatin1String("path"),
                               QLatin1String("."));
    parser.addOption(appPath);

    QCommandLineOption arch(QLatin1String("arch"),
                            QCoreApplication::translate(
                                "main",
                                "Set the fallback architecture, in case the architecture is not "
                                "given in the data itself, to <arch>."),
                            QLatin1String("arch"));
    parser.addOption(arch);

    parser.process(app);

    QPointer<QFile> outfile;
    if (parser.isSet(output)) {
        outfile = new QFile(parser.value(output));
        if (!outfile->open(QIODevice::WriteOnly))
            return CannotOpen;
    } else {
        outfile = new QFile;
        if (!outfile->open(stdout, QIODevice::WriteOnly))
            return CannotOpen;
    }

    QPointer<QIODevice> infile;
    if (parser.isSet(host)) {
        PerfTcpSocket *socket = new PerfTcpSocket(&app);
        infile = socket;
    } else {
        if (parser.isSet(input))
            infile = new QFile(parser.value(input));
        else
            infile = new PerfStdin;
    }

    PerfUnwind unwind(outfile, parser.value(sysroot), parser.isSet(debug) ?
                          parser.value(debug) : parser.value(sysroot) + parser.value(debug),
                      parser.value(extra), parser.value(appPath));
    PerfHeader header(infile);
    PerfAttributes attributes;
    PerfFeatures features;
    PerfData data(infile, &unwind, &header, &attributes);

    if (parser.isSet(arch))
        features.setArchitecture(parser.value(arch).toLatin1());

    QObject::connect(&header, &PerfHeader::finished, [&]() {
        if (!header.isPipe()) {
            attributes.read(infile, &header);
            features.read(infile, &header);
        }

        const QByteArray &featureArch = features.architecture();
        for (uint i = 0; i < PerfRegisterInfo::ARCH_INVALID; ++i) {
            if (featureArch.startsWith(PerfRegisterInfo::s_archNames[i])) {
                unwind.setArchitecture(static_cast<PerfRegisterInfo::Architecture>(i));
                break;
            }
        }

        if (unwind.architecture() == PerfRegisterInfo::ARCH_INVALID) {
            qWarning() << "No information about CPU architecture found. Cannot unwind.";
            app.exit(MissingData);
        }

        QObject::connect(infile.data(), &QIODevice::readyRead, &data, &PerfData::read);
        if (infile->bytesAvailable() > 0)
            data.read();
    });

    QObject::connect(&header, &PerfHeader::error, [&]() {
        app.exit(HeaderError);
    });

    QObject::connect(&data, &PerfData::finished, [&]() {
        exitCode = NoError;
        app.exit(NoError);
    });

    QObject::connect(&data, &PerfData::error, [&]() {
        app.exit(DataError);
    });

    if (parser.isSet(host)) {
        PerfTcpSocket *socket = static_cast<PerfTcpSocket *>(infile.data());
        QObject::connect(socket, &QTcpSocket::disconnected, &data, &PerfData::finishReading);
        QObject::connect(&data, &PerfData::finished, socket, &PerfTcpSocket::readingFinished);
        socket->connectToHost(parser.value(host), parser.value(port).toUShort(),
                              QIODevice::ReadOnly);
    } else {
        if (!infile->open(QIODevice::ReadOnly))
            return CannotOpen;
        header.read();
    }

    return exitCode == -1 ? app.exec() : NoError;
}


void PerfTcpSocket::processError(QAbstractSocket::SocketError error)
{
    if (reading) {
        qWarning() << "socket error" << error << errorString();
        QCoreApplication::instance()->exit(TcpSocketError);
    } // Otherwise ignore the error. We don't need the socket anymore
}


PerfTcpSocket::PerfTcpSocket(QCoreApplication *app) : QTcpSocket(app), reading(true)
{
    connect(this, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(processError(QAbstractSocket::SocketError)));
}

void PerfTcpSocket::readingFinished()
{
    reading = false;
}

#include "main.moc"
