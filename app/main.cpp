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
#include <QDir>
#include <QDebug>
#include <QtEndian>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QScopedPointer>
#include <QAbstractSocket>
#include <QTcpSocket>
#include <limits>

#ifdef Q_OS_WIN
#include <io.h>
#include <fcntl.h>
#endif

enum ErrorCodes {
    NoError,
    TcpSocketError,
    CannotOpen,
    BadMagic,
    HeaderError,
    DataError,
    MissingData,
    InvalidOption
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
                                 "Relative paths are relative to the original file's path. "
                                 "The default is: <sysroot>/usr/lib/debug:~/.debug:.debug ."),
                             QLatin1String("path"),
                             QString("/usr/lib/debug:%1/.debug:.debug").arg(QDir::homePath()));
    parser.addOption(debug);

    QCommandLineOption extra(QLatin1String("extra"),
                             QCoreApplication::translate(
                                 "main", "Look for additional libraries in <path> (default: .)."),
                             QLatin1String("path"));
    parser.addOption(extra);

    QCommandLineOption appPath(QLatin1String("app"),
                               QCoreApplication::translate(
                                   "main", "Look for application binary in <path> (default: .)."),
                               QLatin1String("path"));
    parser.addOption(appPath);

    const auto defaultArch
        = QLatin1String(PerfRegisterInfo::s_archNames[PerfRegisterInfo::s_defaultArchitecture]);
    QCommandLineOption arch(QLatin1String("arch"),
                            QCoreApplication::translate(
                                "main",
                                "Set the fallback architecture, in case the architecture is not "
                                "given in the data itself, to <arch>. "
                                "The default is: %1").arg(defaultArch),
                            QLatin1String("arch"),
                            defaultArch);
    parser.addOption(arch);

    QCommandLineOption kallsymsPath(QLatin1String("kallsyms"),
                               QCoreApplication::translate(
                                   "main", "Path to kallsyms mapping to resolve kernel symbols. "
                                   "The default is: <sysroot>/proc/kallsyms ."),
                               QLatin1String("path"),
                               QLatin1String("/proc/kallsyms"));
    parser.addOption(kallsymsPath);

    QCommandLineOption printStats(QLatin1String("print-stats"),
                               QCoreApplication::translate(
                                   "main", "Print statistics instead of converting the data."));
    parser.addOption(printStats);

    QCommandLineOption bufferSize(QLatin1String("buffer-size"),
                                  QCoreApplication::translate(
                                   "main", "Size of event buffer in kilobytes. This influences how"
                                   " many events get buffered before they get sorted by time and "
                                   " then analyzed. Increase this value when your perf.data file"
                                   " was recorded with a large buffer value (perf record -m)."
                                   " Pass 0 to buffer events until a FINISHED_ROUND event is "
                                   " encountered. Default value is 10MB"),
                                   QLatin1String("buffer-size"), QLatin1String("10240"));
    parser.addOption(bufferSize);

    QCommandLineOption maxFrames(QLatin1String("max-frames"),
                                  QCoreApplication::translate(
                                   "main", "Maximum number of frames that will be unwound."
                                   " Set the value to -1 to unwind as many frames as possible."
                                   " Beware that this can then potentially lead to infinite loops "
                                   " when the stack got corrupted. Default value is 64."),
                                   QLatin1String("max-frames"), QLatin1String("64"));
    parser.addOption(maxFrames);

    parser.process(app);

    QScopedPointer<QFile> outfile;
    if (parser.isSet(output)) {
        outfile.reset(new QFile(parser.value(output)));
        if (!outfile->open(QIODevice::WriteOnly))
            return CannotOpen;
    } else {
        outfile.reset(new QFile);
#ifdef Q_OS_WIN
        _setmode(fileno(stdout), O_BINARY);
#endif
        if (!outfile->open(stdout, QIODevice::WriteOnly))
            return CannotOpen;
    }

    QScopedPointer<QIODevice> infile;
    if (parser.isSet(host)) {
        PerfTcpSocket *socket = new PerfTcpSocket(&app);
        infile.reset(socket);
    } else {
        if (parser.isSet(input))
            infile.reset(new QFile(parser.value(input)));
        else
            infile.reset(new PerfStdin);
    }

    bool ok = false;
    uint maxEventBufferSize = parser.value(bufferSize).toUInt(&ok) * 1024;
    if (!ok) {
        qWarning() << "Failed to parse buffer-size argument. Expected unsigned integer, got:"
                   << parser.value(bufferSize);
        return InvalidOption;
    }

    int maxFramesValue = parser.value(maxFrames).toInt(&ok);
    if (!ok) {
        qWarning() << "Failed to parse max-frames argument. Expected integer, got:"
                   << parser.value(maxFrames);
        return InvalidOption;
    }

    PerfUnwind unwind(outfile.data(), parser.value(sysroot), parser.isSet(debug) ?
                          parser.value(debug) : parser.value(sysroot) + parser.value(debug),
                      parser.value(extra), parser.value(appPath), parser.isSet(kallsymsPath)
                        ? parser.value(kallsymsPath)
                        : parser.value(sysroot) + parser.value(kallsymsPath),
                      parser.isSet(printStats), maxEventBufferSize, maxFramesValue);

    PerfHeader header(infile.data());
    PerfAttributes attributes;
    PerfFeatures features;
    PerfData data(infile.data(), &unwind, &header, &attributes);

    features.setArchitecture(parser.value(arch).toLatin1());

    QObject::connect(&header, &PerfHeader::finished, [&]() {
        if (!header.isPipe()) {
            const qint64 filePos = infile->pos();
            if (!attributes.read(infile.data(), &header)) {
                qWarning() << "Failed to read attributes";
                qApp->exit(DataError);
                return;
            }
            if (!features.read(infile.data(), &header)) {
                qWarning() << "Failed to read features";
                qApp->exit(DataError);
                return;
            }
            infile->seek(filePos);

            // first send features, as it may contain better event descriptions
            unwind.features(features);

            const auto& attrs = attributes.attributes();
            for (auto it = attrs.begin(), end = attrs.end(); it != end; ++it) {
                unwind.attr(PerfRecordAttr(it.value(), {it.key()}));
            }
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
            qApp->exit(MissingData);
            return;
        }

        QObject::connect(infile.data(), &QIODevice::readyRead, &data, &PerfData::read);
        if (infile->bytesAvailable() > 0)
            data.read();
    });

    QObject::connect(&header, &PerfHeader::error, []() {
        qApp->exit(HeaderError);
    });

    QObject::connect(&data, &PerfData::finished, []() {
        qApp->exit(NoError);
    });

    QObject::connect(&data, &PerfData::error, []() {
        qApp->exit(DataError);
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
        QMetaObject::invokeMethod(&header, "read", Qt::QueuedConnection);
    }

    return app.exec();
}


void PerfTcpSocket::processError(QAbstractSocket::SocketError error)
{
    if (reading) {
        qWarning() << "socket error" << error << errorString();
        qApp->exit(TcpSocketError);
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
