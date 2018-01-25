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

#include <QTimer>

#include <cstring>
#include <cstdio>
#include <limits>

PerfStdin::PerfStdin(QObject *parent) : QIODevice(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &PerfStdin::receiveData);
}

PerfStdin::~PerfStdin()
{
    if (isOpen())
        close();
}

bool PerfStdin::open(QIODevice::OpenMode mode)
{
    if (!(mode & QIODevice::ReadOnly) || (mode & QIODevice::WriteOnly))
        return false;

    if (QIODevice::open(mode)) {
        m_buffer.resize(s_minBufferSize);
        m_timer.start();
        return true;
    } else {
        return false;
    }
}

void PerfStdin::close()
{
    m_timer.stop();
    QIODevice::close();
}

qint64 PerfStdin::readData(char *data, qint64 maxlen)
{
    if (maxlen <= 0)
        return 0;

    qint64 read = 0;
    do {
        Q_ASSERT(m_buffer.length() >= m_bufferPos);
        Q_ASSERT(m_buffer.length() >= m_bufferUsed);
        Q_ASSERT(m_bufferPos <= m_bufferUsed);
        const size_t buffered = static_cast<size_t>(qMin(bufferedAvailable(), maxlen - read));
        memcpy(data + read, m_buffer.constData() + m_bufferPos, buffered);
        m_bufferPos += buffered;
        read += buffered;
    } while (read < maxlen && fillBuffer(maxlen) > 0);

    Q_ASSERT(read > 0 || bufferedAvailable() == 0);
    return (read == 0 && stdinAtEnd()) ? -1 : read;
}

qint64 PerfStdin::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);
    return -1;
}

void PerfStdin::receiveData()
{
    if (fillBuffer() > 0)
        emit readyRead();
    else if (stdinAtEnd())
        close();
}

void PerfStdin::resizeBuffer(int newSize)
{
    QByteArray newBuffer(newSize, Qt::Uninitialized);
    std::memcpy(newBuffer.data(), m_buffer.data() + m_bufferPos,
                static_cast<size_t>(m_bufferUsed - m_bufferPos));
    qSwap(m_buffer, newBuffer);
    m_bufferUsed -= m_bufferPos;
    Q_ASSERT(m_buffer.length() >= m_bufferUsed);
    m_bufferPos = 0;
}

qint64 PerfStdin::fillBuffer(qint64 targetBufferSize)
{
    if (m_bufferUsed == m_bufferPos)
        m_bufferPos = m_bufferUsed = 0;

    targetBufferSize = qMin(targetBufferSize, static_cast<qint64>(s_maxBufferSize));
    if (targetBufferSize > m_buffer.length())
        resizeBuffer(static_cast<int>(targetBufferSize));

    if (m_bufferUsed == m_buffer.length()) {
        if (m_bufferPos == 0) {
            resizeBuffer(m_bufferUsed <= s_maxBufferSize / 2 ? m_bufferUsed * 2
                                                             : s_maxBufferSize);
        } else {
            resizeBuffer(m_bufferUsed);
        }
    }

    const size_t read = fread(m_buffer.data() + m_bufferUsed, 1,
                              static_cast<size_t>(m_buffer.length() - m_bufferUsed), stdin);
    m_bufferUsed += read;
    Q_ASSERT(m_buffer.length() >= m_bufferUsed);
    return static_cast<qint64>(read);
}

bool PerfStdin::stdinAtEnd() const
{
    return feof(stdin) || ferror(stdin);
}

bool PerfStdin::isSequential() const
{
    return true;
}

qint64 PerfStdin::bytesAvailable() const
{
    return bufferedAvailable() + QIODevice::bytesAvailable();
}
