/****************************************************************************
**
** Copyright (C) 2020 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Milian Wolff <milian.wolff@kdab.com>
** Contact: http://www.qt.io/licensing/
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

#include <libdwfl.h>

#include <QVector>
#include <QHash>

#include <algorithm>

/// @return the demangled symbol name
QByteArray demangle(const QByteArray &mangledName);

struct DwarfRange
{
    Dwarf_Addr low;
    Dwarf_Addr high;

    bool contains(Dwarf_Addr addr) const
    {
        return low <= addr && addr < high;
    }
};

/// cache of dwarf ranges for a given Dwarf_Die
struct DieRanges
{
    Dwarf_Die die;
    QVector<DwarfRange> ranges;

    bool contains(Dwarf_Addr addr) const
    {
        return std::any_of(ranges.begin(), ranges.end(), [addr](DwarfRange range) {
            return range.contains(addr);
        });
    }
};

/// cache of sub program DIE, its ranges and the accompanying die name
class SubProgramDie
{
public:
    SubProgramDie() = default;
    SubProgramDie(Dwarf_Die die);
    ~SubProgramDie();

    bool isEmpty() const { return m_ranges.ranges.isEmpty(); }
    /// @p offset a bias-corrected offset
    bool contains(Dwarf_Addr offset) const { return m_ranges.contains(offset); }
    Dwarf_Die *die() { return &m_ranges.die; }

private:
    DieRanges m_ranges;
};

/// cache of dwarf ranges for a CU DIE and child sub programs
class CuDieRangeMapping
{
public:
    CuDieRangeMapping() = default;
    CuDieRangeMapping(Dwarf_Die cudie, Dwarf_Addr bias);
    ~CuDieRangeMapping();

    bool isEmpty() const { return m_cuDieRanges.ranges.isEmpty(); }
    bool contains(Dwarf_Addr addr) const { return m_cuDieRanges.contains(addr); }
    Dwarf_Addr bias() { return m_bias; }
    Dwarf_Die *cudie() { return &m_cuDieRanges.die; }

    /// On first call this will visit the CU DIE to cache all subprograms
    /// @return the DW_TAG_subprogram DIE that contains @p offset
    /// @p offset a bias-corrected address to find a subprogram for
    SubProgramDie *findSubprogramDie(Dwarf_Addr offset);

    /// @return a fully qualified, demangled symbol name for @p die
    QByteArray dieName(Dwarf_Die *die);

private:
    void addSubprograms();

    Dwarf_Addr m_bias = 0;
    DieRanges m_cuDieRanges;
    QVector<SubProgramDie> m_subPrograms;
    QHash<Dwarf_Off, QByteArray> m_dieNameCache;
};

/**
 * @return all DW_TAG_inlined_subroutine DIEs that contain @p offset
 * @p subprogram DIE sub tree that should be traversed to look for inlined scopes
 * @p offset bias-corrected address that is checked against the dwarf ranges of the DIEs
 */
QVector<Dwarf_Die> findInlineScopes(Dwarf_Die *subprogram, Dwarf_Addr offset);

/**
 * @return the absolute source path for a @p path that may be absolute already or relative to the compilation directory
 * @p path either an absolute that will be passed through directly or a path relative to the compilation directory
 * @p cuDie the CU DIE that will be queried for the compilation directory to resolve relative paths
 * @sa findSourceLocation
 */
QByteArray absoluteSourcePath(const char *path, Dwarf_Die *cuDie);

struct DwarfSourceLocation
{
    QByteArray file;
    int line = -1;
    int column = -1;

    explicit operator bool() const
    {
        return !file.isEmpty();
    }
};
/**
 * @return the absolute file name, line number and column for the instruction at the given @p offset in @p cuDie
 * @p cuDie CU DIE that should be queried
 * @p offset bias-corrected address of an instruction for which the information should be found
 * @sa CuDieRangeMapping
 */
DwarfSourceLocation findSourceLocation(Dwarf_Die* cuDie, Dwarf_Addr offset);

/**
 * This cache makes it easily possible to find a CU DIE (i.e. Compilation Unit Debugging Information Entry)
 * based on a
 */
class PerfDwarfDieCache
{
public:
    PerfDwarfDieCache(Dwfl_Module *mod = nullptr);
    ~PerfDwarfDieCache();

    /// @p addr absolute address, not bias-corrected
    CuDieRangeMapping *findCuDie(Dwarf_Addr addr);

public:
    QVector<CuDieRangeMapping> m_cuDieRanges;
};
QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(DwarfRange, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(PerfDwarfDieCache, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(DieRanges, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(CuDieRangeMapping, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(Dwarf_Die, Q_MOVABLE_TYPE);
QT_END_NAMESPACE
