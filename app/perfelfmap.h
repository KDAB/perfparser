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

#ifndef PERFELFMAP_H
#define PERFELFMAP_H

#include <QFileInfo>
#include <QVector>

class PerfElfMap
{
public:
    struct ElfInfo {
        ElfInfo(const QFileInfo &localFile = QFileInfo(), quint64 addr = 0, quint64 length = 0,
                quint64 pgoff = 0, const QByteArray &originalFileName = {}) :
            localFile(localFile),
            originalFileName(originalFileName.isEmpty()
                ? localFile.fileName().toLocal8Bit()
                : originalFileName),
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

        bool operator==(const ElfInfo& rhs) const
        {
            return isFile() == rhs.isFile()
                && (!isFile() || localFile == rhs.localFile)
                && originalFileName == rhs.originalFileName
                && addr == rhs.addr
                && length == rhs.length
                && pgoff == rhs.pgoff;
        }

        QFileInfo localFile;
        QByteArray originalFileName;
        quint64 addr;
        quint64 length;
        quint64 pgoff;
    };

    bool registerElf(quint64 addr, quint64 len, quint64 pgoff,
                     const QFileInfo &fullPath, const QByteArray &originalFileName = {});
    ElfInfo findElf(quint64 ip) const;

    bool isEmpty() const
    {
        return m_elfs.isEmpty();
    }

    bool isAddressInRange(quint64 addr) const;

private:
    // elf sorted by start address
    QVector<ElfInfo> m_elfs;
};
Q_DECLARE_TYPEINFO(PerfElfMap::ElfInfo, Q_MOVABLE_TYPE);

QDebug operator<<(QDebug stream, const PerfElfMap::ElfInfo& info);

#endif
