/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd
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

#include <QFileInfo>
#include <QVector>
#include <limits>

class PerfElfMap : public QObject
{
    Q_OBJECT
public:
    struct ElfInfo {
        enum {
            INVALID_BASE_ADDR = std::numeric_limits<quint64>::max()
        };
        explicit ElfInfo(const QFileInfo &localFile = QFileInfo(), quint64 addr = 0,
                         quint64 length = 0, quint64 pgoff = 0,
                         const QByteArray &originalFileName = {},
                         const QByteArray &originalPath = {}) :
            localFile(localFile),
            originalFileName(originalFileName.isEmpty()
                ? localFile.fileName().toLocal8Bit()
                : originalFileName),
            originalPath(originalPath.isEmpty()
                ? localFile.absoluteFilePath().toLocal8Bit()
                : originalPath),
            addr(addr), length(length), pgoff(pgoff)
        {}

        bool isValid() const
        {
            return length > 0;
        }

        bool isFile() const
        {
            return localFile.isFile();
        }

        bool hasBaseAddr() const
        {
            return baseAddr != INVALID_BASE_ADDR;
        }

        bool operator==(const ElfInfo& rhs) const
        {
            return isFile() == rhs.isFile()
                && (!isFile() || localFile == rhs.localFile)
                && originalFileName == rhs.originalFileName
                && originalPath == rhs.originalPath
                && addr == rhs.addr
                && length == rhs.length
                && pgoff == rhs.pgoff
                && baseAddr == rhs.baseAddr;
        }

        bool operator!=(const ElfInfo& rhs) const
        {
            return !operator==(rhs);
        }

        QFileInfo localFile;
        QByteArray originalFileName;
        QByteArray originalPath;
        quint64 addr;
        quint64 length;
        quint64 pgoff;
        quint64 baseAddr = INVALID_BASE_ADDR;
        quint64 dwflStart = 0;
        quint64 dwflEnd = 0;
    };

    explicit PerfElfMap(QObject *parent = nullptr);
    ~PerfElfMap();

    void registerElf(quint64 addr, quint64 len, quint64 pgoff,
                     const QFileInfo &fullPath,
                     const QByteArray &originalFileName = {},
                     const QByteArray &originalPath = {});
    ElfInfo findElf(quint64 ip) const;
    void updateElf(quint64 addr, quint64 dwflStart, quint64 dwflEnd);

    bool isEmpty() const
    {
        return m_elfs.isEmpty();
    }

    bool isAddressInRange(quint64 addr) const;

signals:
    void aboutToInvalidate(const ElfInfo &elf);

private:
    // elf sorted by start address
    QVector<ElfInfo> m_elfs;
    // last registered elf with zero pgoff
    ElfInfo m_lastBase;
};

QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(PerfElfMap::ElfInfo, Q_MOVABLE_TYPE);
QT_END_NAMESPACE

QDebug operator<<(QDebug stream, const PerfElfMap::ElfInfo& info);
