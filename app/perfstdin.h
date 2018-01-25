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

#pragma once

#include <QIODevice>
#include <QTimer>

class PerfStdin : public QIODevice
{
public:
    PerfStdin(QObject *parent = nullptr);
    ~PerfStdin();

    bool open(OpenMode mode) override;
    void close() override;
    bool isSequential() const override;
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    static const int s_minBufferSize = 1 << 10;
    static const int s_maxBufferSize = 1 << 30;

    void receiveData();
    void resizeBuffer(int newSize);
    qint64 fillBuffer(qint64 targetSize = -1);
    qint64 bufferedAvailable() const { return m_bufferUsed - m_bufferPos; }
    bool stdinAtEnd() const;

    QTimer m_timer;
    QByteArray m_buffer;
    int m_bufferPos = 0;
    int m_bufferUsed = 0;
};
