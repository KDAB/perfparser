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

#include "perfstdin.h"
#include <cstdio>
#include <limits>

bool PerfStdin::open(QIODevice::OpenMode mode)
{
    if (!(mode & QIODevice::ReadOnly) || (mode & QIODevice::WriteOnly))
        return false;

    return QIODevice::open(mode);
}

qint64 PerfStdin::readData(char *data, qint64 maxlen)
{
    size_t read = fread(data, 1, maxlen, stdin);
    if (feof(stdin) || ferror(stdin))
        close();
    if (read == 0 && maxlen > 0) {
        return -1;
    } else {
        return read;
    }
}

qint64 PerfStdin::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);
    return -1;
}

bool PerfStdin::isSequential() const
{
    return true;
}

qint64 PerfStdin::bytesAvailable() const
{
    return isOpen() ? std::numeric_limits<qint64>::max() : 0;
}
