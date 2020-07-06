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

#include "perfattributes.h"
#include "perfdata.h"
#include "perffeatures.h"
#include "perfheader.h"
#include "perfregisterinfo.h"
#include "perfstdin.h"
#include "perfunwind.h"

#include <QAbstractSocket>
#include <QTemporaryFile>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QScopedPointer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <cstring>

#ifdef Q_OS_LINUX
#include <signal.h>
#endif

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
    InvalidOption,
    BufferingError
};

class PerfTcpSocket : public QTcpSocket {
    Q_OBJECT
public:
    PerfTcpSocket(QCoreApplication *app, const QString &host, quint16 port);
    void tryConnect();

public slots:
    void processError(QAbstractSocket::SocketError error);

private:
    QString host;
    quint16 port = 0;
    quint16 tries = 0;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QLatin1String("perfparser"));
    app.setApplicationVersion(QLatin1String("4.13"));

    if (qEnvironmentVariableIsSet("PERFPARSER_DEBUG_WAIT")) {
#ifdef Q_OS_LINUX
        qWarning("PERFPARSER_DEBUG_WAIT is set, halting perfparser.");
        qWarning("Continue with \"kill -SIGCONT %lld\" or by attaching a debugger.", app.applicationPid());
        kill(app.applicationPid(), SIGSTOP);
#else
        qWarning("PERFPARSER_DEBUG_WAIT is set, but this only works on linux. Ignoring.");
#endif
    }

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
                                   "main", "Look for system libraries in <path> (default: %1).")
                               .arg(QDir::rootPath()),
                               QLatin1String("path"),
                               QDir::rootPath());
    parser.addOption(sysroot);

    const auto defaultDebug = PerfUnwind::defaultDebugInfoPath();
    QCommandLineOption debug(QLatin1String("debug"),
                             QCoreApplication::translate(
                                 "main",
                                 "Look for debug information in <path>. "
                                 "You can specify multiple paths separated by ':'. "
                                 "Relative paths are relative to the original file's path. "
                                 "The default is: <sysroot>%1 .").arg(defaultDebug),
                             QLatin1String("path"), defaultDebug);
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

    const auto defaultArch = QLatin1String(PerfRegisterInfo::defaultArchitecture());
    QCommandLineOption arch(QLatin1String("arch"),
                            QCoreApplication::translate(
                                "main",
                                "Set the fallback architecture, in case the architecture is not "
                                "given in the data itself, to <arch>. "
                                "The default is: %1").arg(defaultArch),
                            QLatin1String("arch"),
                            defaultArch);
    parser.addOption(arch);

    const auto defaultKallsyms = PerfUnwind::defaultKallsymsPath();
    QCommandLineOption kallsymsPath(QLatin1String("kallsyms"),
                                    QCoreApplication::translate(
                                        "main", "Path to kallsyms mapping to resolve kernel "
                                        "symbols. The default is: <sysroot>%1 .")
                                    .arg(defaultKallsyms), QLatin1String("path"), defaultKallsyms);
    parser.addOption(kallsymsPath);

    QCommandLineOption printStats(QLatin1String("print-stats"),
                               QCoreApplication::translate(
                                   "main", "Print statistics instead of converting the data."));
    parser.addOption(printStats);

    QCommandLineOption bufferSize(QLatin1String("buffer-size"),
                                  QCoreApplication::translate(
                                   "main", "Initial size of event buffer in kilobytes. This"
                                   " influences how many events get buffered before they get"
                                   " sorted by time and then analyzed. Increase this value when"
                                   " your perf.data file was recorded with a large buffer value"
                                   " (perf record -m). Pass 0 to buffer events until a"
                                   " FINISHED_ROUND event is encountered. perfparser will switch to"
                                   " automatic buffering by FINISHED_ROUND events if either the"
                                   " data indicates that the tace was recorded with a version of"
                                   " perf >= 3.17, or a FINISHED_ROUND event is encountered and no"
                                   " time order violations have occurred before. When encountering"
                                   " a time order violation, perfparser will switch back to dynamic"
                                   " buffering using buffer-size and max-buffer-size."
                                   " The default value is 32MB."),
                                   QLatin1String("buffer-size"), QString::number(1 << 15));
    parser.addOption(bufferSize);

    QCommandLineOption maxBufferSize(QLatin1String("max-buffer-size"),
                                  QCoreApplication::translate(
                                   "main", "Maximum size of event buffer in kilobytes. perfparser"
                                   " increases the size of the event buffer when time order"
                                   " violations are detected. It will never increase it beyond this"
                                   " value, though. The default value is 1GB"),
                                   QLatin1String("max-buffer-size"), QString::number(1 << 20));
    parser.addOption(maxBufferSize);

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
        PerfTcpSocket *socket = new PerfTcpSocket(&app, parser.value(host),
                                                  parser.value(port).toUShort());
        infile.reset(socket);
    } else {
        if (parser.isSet(input)) {
            infile.reset(new QFile(parser.value(input)));
        } else {
#ifdef Q_OS_WIN
            _setmode(fileno(stdin), O_BINARY);
#endif
            infile.reset(new PerfStdin);
        }
    }

    bool ok = false;
    uint targetEventBufferSize = parser.value(bufferSize).toUInt(&ok) * 1024;
    if (!ok) {
        qWarning() << "Failed to parse buffer-size argument. Expected unsigned integer, got:"
                   << parser.value(bufferSize);
        return InvalidOption;
    }

    uint maxEventBufferSize = parser.value(maxBufferSize).toUInt(&ok) * 1024;
    if (!ok) {
        qWarning() << "Failed to parse buffer-size argument. Expected unsigned integer, got:"
                   << parser.value(maxBufferSize);
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
                      parser.value(extra), parser.value(appPath), parser.isSet(printStats));

    unwind.setKallsymsPath(parser.isSet(kallsymsPath)
                           ? parser.value(kallsymsPath)
                           : (parser.value(sysroot) + parser.value(kallsymsPath)));

    unwind.setIgnoreKallsymsBuildId(parser.isSet(kallsymsPath));

    unwind.setTargetEventBufferSize(targetEventBufferSize);
    unwind.setMaxEventBufferSize(maxEventBufferSize);
    unwind.setMaxUnwindFrames(maxFramesValue);

    PerfHeader header(infile.data());
    PerfAttributes attributes;
    PerfFeatures features;
    PerfData data(&unwind, &header, &attributes);

    features.setArchitecture(parser.value(arch).toLatin1());

    auto readFileHeader = [&]() {
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
    };

    auto readData = [&]() {
        const QByteArray &featureArch = features.architecture();
        unwind.setArchitecture(PerfRegisterInfo::archByName(featureArch));

        if (unwind.architecture() == PerfRegisterInfo::ARCH_INVALID) {
            qWarning() << "No information about CPU architecture found. Cannot unwind.";
            qApp->exit(MissingData);
            return;
        }

        data.setSource(infile.data());
        QObject::connect(infile.data(), &QIODevice::aboutToClose, &data, &PerfData::finishReading);
        QObject::connect(&data, &PerfData::finished, infile.data(), [&](){ infile->disconnect(); });
        QObject::connect(infile.data(), &QIODevice::readyRead, &data, &PerfData::read);
        if (infile->bytesAvailable() > 0)
            data.read();
    };

    auto writeBytes = [](QIODevice *target, const char *data, qint64 length) {
        qint64 pos = 0;
        while (pos < length) {
            const qint64 written = target->write(data + pos, length - pos);
            if (written < 0)
                return false;
            pos += written;
        }
        return true;
    };

    QScopedPointer<QIODevice> tempfile;
    auto bufferSequentialData = [&](){
        QByteArray buffer(1 << 25, Qt::Uninitialized);
        const qint64 read = infile->read(buffer.data(), buffer.length());
        if (read < 0) {
            qWarning() << "Failed to read from input.";
            qApp->exit(BufferingError);
            return;
        }

        if (!writeBytes(tempfile.data(), buffer.data(), read)) {
            qWarning() << "Failed to write buffer file.";
            qApp->exit(BufferingError);
            return;
        }
    };

    QObject::connect(&header, &PerfHeader::finished, [&]() {
        unwind.setByteOrder(static_cast<QSysInfo::Endian>(header.byteOrder()));
        if (!header.isPipe()) {
            if (infile->isSequential()) {
                qWarning() << "Reading a non-pipe perf.data from a stream requires buffering.";
                tempfile.reset(new QTemporaryFile);
                if (!tempfile->open(QIODevice::ReadWrite)) {
                    qWarning() << "Failed to open buffer file.";
                    qApp->exit(BufferingError);
                    return;
                }

                // We've checked this when parsing the header.
                Q_ASSERT(header.size() <= std::numeric_limits<int>::max());
                const QByteArray fakeHeader(static_cast<int>(header.size()), 0);
                if (!writeBytes(tempfile.data(), fakeHeader.data(), fakeHeader.length())) {
                    qWarning() << "Failed to write fake header to buffer file.";
                    qApp->exit(BufferingError);
                    return;
                }

                QObject::connect(infile.data(), &QIODevice::readyRead, bufferSequentialData);
                QObject::connect(infile.data(), &QIODevice::aboutToClose, [&]() {
                    infile->disconnect();
                    infile.swap(tempfile);
                    if (!infile->reset()) {
                        qWarning() << "Cannot reset buffer file.";
                        qApp->exit(BufferingError);
                        return;
                    }
                    readFileHeader();
                    readData();
                });

                if (infile->bytesAvailable() > 0)
                    bufferSequentialData();
            } else {
                readFileHeader();
                readData();
            }
        } else {
            readData();
        }
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
        socket->tryConnect();
    } else {
        if (!infile->open(QIODevice::ReadOnly))
            return CannotOpen;
        if (qobject_cast<QFile *>(infile.data())) // We don't get readyRead then ...
            QTimer::singleShot(0, &header, &PerfHeader::read);
    }

    return app.exec();
}


void PerfTcpSocket::processError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::RemoteHostClosedError)
        return;

    qWarning() << "socket error" << error << errorString();
    if (state() == QAbstractSocket::ConnectedState || tries > 10)
        qApp->exit(TcpSocketError);
    else
        QTimer::singleShot(1 << tries, this, &PerfTcpSocket::tryConnect);
}

PerfTcpSocket::PerfTcpSocket(QCoreApplication *app, const QString &host, quint16 port) :
    QTcpSocket(app), host(host), port(port)
{
    connect(this, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &PerfTcpSocket::processError);
    connect(this, &QAbstractSocket::disconnected, this, &QIODevice::close);
}

void PerfTcpSocket::tryConnect()
{
    ++tries;
    connectToHost(host, port, QIODevice::ReadOnly);
}

#include "main.moc"
