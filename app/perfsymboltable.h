/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/
#ifndef PERFSYMBOLTABLE_H
#define PERFSYMBOLTABLE_H

#include "perfdata.h"
#include "perfunwind.h"

#include <libdwfl.h>
#include <QObject>
#include <QFileInfo>
#include <QMap>

class PerfSymbolTable
{
public:
    PerfSymbolTable(quint32 pid, Dwfl_Callbacks *callbacks, PerfUnwind *parent);
    ~PerfSymbolTable();

    struct ElfInfo {
        ElfInfo(const QFileInfo &file = QFileInfo(), quint64 length = 0, bool found = true) :
            file(file), length(length), found(found) {}
        QFileInfo file;
        quint64 length;
        bool found;
    };

    struct PerfMapSymbol {
        PerfMapSymbol(quint64 start = 0, quint64 length = 0, QByteArray name = QByteArray()) :
            start(start), length(length), name(name) {}
        quint64 start;
        quint64 length;
        QByteArray name;
    };

    // Announce an mmap. Invalidate the symbol and address cache and clear the dwfl if it overlaps
    // with an existing one.
    void registerElf(const PerfRecordMmap &mmap, const QString &appPath,
                     const QString &systemRoot, const QString &extraLibsPath);

    // Report an mmap to dwfl and parse it for symbols and inlines, or simply return it if dwfl has
    // it already
    Dwfl_Module *reportElf(quint64 ip, const ElfInfo **info = 0);

    // Look up a frame and all its inline parents and append them to the given vector.
    // If the frame hits an elf that hasn't been reported, yet, report it.
    PerfUnwind::Frame lookupFrame(Dwarf_Addr ip, bool isKernel = false);

    void updatePerfMap();
    bool containsAddress(quint64 address) const;

    Dwfl *attachDwfl(quint32 pid, void *arg);
    void clearCache();

private:
    QFile m_perfMapFile;
    QVector<PerfMapSymbol> m_perfMap;
    QHash<Dwarf_Addr, PerfUnwind::Frame> m_addrCache;

    PerfUnwind *m_unwind;
    Dwfl *m_dwfl;

    QMap<quint64, ElfInfo> m_elfs; // needs to be sorted
    Dwfl_Callbacks *m_callbacks;
    QByteArray symbolFromPerfMap(quint64 ip, GElf_Off *offset) const;
};

QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(PerfSymbolTable::PerfMapSymbol, Q_MOVABLE_TYPE);
QT_END_NAMESPACE

#endif // PERFSYMBOLTABLE_H
