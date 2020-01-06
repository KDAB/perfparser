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

#pragma once

#include "perfaddresscache.h"
#include "perfdata.h"
#include "perfelfmap.h"
#include "perfunwind.h"

#include <libdwfl.h>

#include <QObject>

class CuDieRanges;

class PerfSymbolTable
{
public:
    PerfSymbolTable(qint32 pid, Dwfl_Callbacks *callbacks, PerfUnwind *parent);
    ~PerfSymbolTable();

    struct PerfMapSymbol {
        PerfMapSymbol(quint64 start = 0, quint64 length = 0, QByteArray name = QByteArray()) :
            start(start), length(length), name(name) {}
        quint64 start;
        quint64 length;
        QByteArray name;
    };

    // Announce an mmap. Invalidate the symbol and address cache and clear the dwfl if it overlaps
    // with an existing one.
    void registerElf(const PerfRecordMmap &mmap, const QByteArray &buildId);

    PerfElfMap::ElfInfo findElf(quint64 ip) const;

    // Find the module for the given address and report it if needed
    Dwfl_Module *module(quint64 addr);
    Dwfl_Module *module(quint64 addr, const PerfElfMap::ElfInfo &elf);
    int findDebugInfo(Dwfl_Module *module, const char *moduleName, Dwarf_Addr base,
                      const char *file, const char *debugLink,
                      GElf_Word crc, char **debugInfoFilename);

    // Look up a frame and all its inline parents and append them to the given vector.
    // If the frame hits an elf that hasn't been reported, yet, report it.
    int lookupFrame(Dwarf_Addr ip, bool isKernel, bool *isInterworking);

    void updatePerfMap();
    bool containsAddress(quint64 address) const;

    Dwfl *attachDwfl(void *arg);
    void clearCache();
    bool cacheIsDirty() const { return m_cacheIsDirty; }

private:
    // Report an mmap to dwfl and parse it for symbols and inlines, or simply return it if dwfl has
    // it already
    Dwfl_Module *reportElf(const PerfElfMap::ElfInfo& elf);
    QFileInfo findFile(const char *path, const QString &fileName,
                       const QByteArray &buildId = QByteArray()) const;

    class ElfAndFile {
    public:
        ElfAndFile() {}
        ElfAndFile(const QFileInfo &fullPath);
        ElfAndFile(ElfAndFile &&other);
        ElfAndFile &operator=(ElfAndFile &&other);
        ElfAndFile(const ElfAndFile &other) = delete;
        ElfAndFile &operator=(const ElfAndFile &other) = delete;
        ~ElfAndFile();

        Elf *elf() const { return m_elf; }

    private:
        void clear();

        Elf *m_elf = nullptr;
        int m_file = -1;
    };

    QFile m_perfMapFile;
    QVector<PerfMapSymbol> m_perfMap;
    bool m_cacheIsDirty;

    PerfUnwind *m_unwind;
    Dwfl *m_dwfl;
    // elf used to detect architecture
    ElfAndFile m_firstElf;

    PerfElfMap m_elfs;
    PerfAddressCache::OffsetAddressCache m_invalidAddressCache;
    QHash<Dwfl_Module*, CuDieRanges> m_cuDieRanges;
    Dwfl_Callbacks *m_callbacks;
    qint32 m_pid;

    QByteArray symbolFromPerfMap(quint64 ip, GElf_Off *offset) const;
    int parseDie(Dwarf_Die *top, qint32 binaryId, qint32 binaryPathId, bool isKernel,
                 Dwarf_Files *files, Dwarf_Addr entry, qint32 parentLocationId);
    int insertSubprogram(Dwarf_Die *top, Dwarf_Addr entry, qint32 binaryId, qint32 binaryPathId,
                         qint32 inlineParent, bool isKernel);
    qint32 parseDwarf(Dwarf_Die *cudie, Dwarf_Die *subroutine, Dwarf_Addr bias, qint32 binaryId,
                      qint32 binaryPathId, bool isKernel);
};

QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(PerfSymbolTable::PerfMapSymbol, Q_MOVABLE_TYPE);
QT_END_NAMESPACE
