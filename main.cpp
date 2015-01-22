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

#include <QFile>
#include <QDebug>
#include <QtEndian>
#include <limits>

enum ErrorCodes {
    NoError,
    CannotOpen,
    BadMagic,
    MissingData
};


const QLatin1String DefaultFileName("perf.data");

// TODO: parse from parameters
const QByteArray systemRoot("/home/ulf/sysroot");
const QByteArray extraLibs("/home/ulf/Qt/Boot2Qt-3.x/beaglebone-eLinux/qt5");
const QByteArray appPath("/home/ulf/sysroot/usr/bin");

int main(int argc, char *argv[])
{
    QFile file(argc > 1 ? QLatin1String(argv[1]) : DefaultFileName);

    if (!file.open(QIODevice::ReadOnly))
        return CannotOpen;

    PerfAttributes attributes;
    PerfFeatures features;
    PerfData data;
    PerfHeader header;
    header.read(&file);
    if (!header.isPipe()) {
        attributes.read(&file, &header);
        features.read(&file, &header);
    }
    data.read(&file, &header, &attributes);

    QSet<quint32> pids;
    foreach (const PerfRecordMmap &mmap, data.mmapRecords()) {
        // UINT32_MAX is kernel
        if (mmap.pid() != std::numeric_limits<quint32>::max())
            pids << mmap.pid();
    }

    foreach (quint32 pid, pids) {
        PerfUnwind unwind(pid, &header, &features, systemRoot, extraLibs, appPath);

        if (unwind.architecture() == PerfRegisterInfo::s_numArchitectures) {
            qWarning() << "No information about CPU architecture found. Cannot unwind.";
            return MissingData;
        }

        foreach (const PerfRecordMmap &mmap, data.mmapRecords())
            unwind.registerElf(mmap);

        foreach (const PerfRecordSample &sample, data.sampleRecords())
            unwind.analyze(sample);
    }

    return NoError;
}
