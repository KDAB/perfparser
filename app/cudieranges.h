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

#include <dwarf.h>

#include <QVector>
#include <QDebug>

enum class WalkResult
{
    Recurse,
    Skip,
    Return
};
template<typename Callback>
WalkResult walkDieTree(const Callback &callback, Dwarf_Die *die)
{
    auto result = callback(die);
    if (result != WalkResult::Recurse)
        return result;

    Dwarf_Die childDie;
    if (dwarf_child(die, &childDie) == 0) {
        result = walkDieTree(callback, &childDie);
        if (result == WalkResult::Return)
            return result;

        Dwarf_Die siblingDie;
        while (dwarf_siblingof(&childDie, &siblingDie) == 0) {
            result = walkDieTree(callback, &siblingDie);
            if (result == WalkResult::Return)
                return result;
            childDie = siblingDie;
        }
    }
    return WalkResult::Skip;
}

struct DwarfRange
{
    Dwarf_Addr low;
    Dwarf_Addr high;

    bool contains(Dwarf_Addr addr) const
    {
        return low <= addr && addr < high;
    }
};

template<typename Callback>
void walkRanges(const Callback &callback, Dwarf_Die *die)
{
    Dwarf_Addr low = 0;
    Dwarf_Addr high = 0;
    Dwarf_Addr base = 0;
    ptrdiff_t rangeOffset = 0;
    while ((rangeOffset = dwarf_ranges(die, rangeOffset, &base, &low, &high)) > 0) {
        if (!callback(DwarfRange{low, high}))
            return;
    }
}

inline bool dieContainsAddress(Dwarf_Die *die, Dwarf_Addr address)
{
    bool contained = false;
    walkRanges([&contained, address](DwarfRange range) {
        if (range.contains(address)) {
            contained = true;
            return false;
        }
        return true;
    }, die);
    return contained;
}

inline QVector<Dwarf_Die> findInlineScopes(Dwarf_Die *subprogram, Dwarf_Addr offset)
{
    QVector<Dwarf_Die> scopes;
    walkDieTree([offset, &scopes](Dwarf_Die *die) {
        if (dwarf_tag(die) != DW_TAG_inlined_subroutine)
            return WalkResult::Recurse;
        if (dieContainsAddress(die, offset)) {
            scopes.append(*die);
            return WalkResult::Recurse;
        }
        return WalkResult::Skip;
    }, subprogram);
    return scopes;
}

class CuDieRanges
{
public:
    struct DieRanges
    {
        Dwarf_Die die;
        QVector<DwarfRange> ranges;

        bool contains(Dwarf_Addr addr) const
        {
            return std::any_of(ranges.begin(), ranges.end(), [addr](const DwarfRange &range) {
                return range.contains(addr);
            });
        }
    };
    struct CuDieRangeMapping
    {
        Dwarf_Addr bias;
        DieRanges cuDieRanges;
        QVector<DieRanges> subPrograms;

        Dwarf_Die *cudie() { return &cuDieRanges.die; }

        Dwarf_Die *findSubprogramDie(Dwarf_Addr offset)
        {
            if (subPrograms.isEmpty()) {
                addSubprograms();
            }

            auto it = std::find_if(subPrograms.begin(), subPrograms.end(),
                                [offset](const DieRanges &dieRanges) {
                                        return dieRanges.contains(offset);
                                });
            if (it == subPrograms.end())
                return nullptr;

            return &it->die;
        }

    private:
        // see libdw_visit_scopes.c
        static bool mayHaveScopes(Dwarf_Die *die)
        {
            switch (dwarf_tag(die))
            {
            /* DIEs with addresses we can try to match.  */
            case DW_TAG_compile_unit:
            case DW_TAG_module:
            case DW_TAG_lexical_block:
            case DW_TAG_with_stmt:
            case DW_TAG_catch_block:
            case DW_TAG_try_block:
            case DW_TAG_entry_point:
            case DW_TAG_inlined_subroutine:
            case DW_TAG_subprogram:
                return true;

            /* DIEs without addresses that can own DIEs with addresses.  */
            case DW_TAG_namespace:
            case DW_TAG_class_type:
            case DW_TAG_structure_type:
                return true;

            /* Other DIEs we have no reason to descend.  */
            default:
                break;
            }
            return false;
        }

        void addSubprograms()
        {
#ifndef NDEBUG
            qDebug() << "adding subprograms" << dwarf_dieoffset(cudie()) << dwarf_diename(cudie());
#endif
            walkDieTree([this](Dwarf_Die *die) {
#ifndef NDEBUG
                qDebug() << hex << dwarf_tag(die) << dwarf_diename(die) << dwarf_dieoffset(die);
#endif
                if (!mayHaveScopes(die))
                    return WalkResult::Skip;

                if (dwarf_tag(die) == DW_TAG_subprogram) {
                    DieRanges ranges;
                    ranges.die = *die;

                    walkRanges([&ranges](DwarfRange range) {
#ifndef NDEBUG
                        qDebug() << range.low << range.high;
#endif
                        ranges.ranges.append(range);
                        return true;
                    }, die);
#ifndef NDEBUG
                    qDebug() << dwarf_diename(die) << hex << dwarf_dieoffset(die) << ranges.ranges.size();
#endif

                    if (!ranges.ranges.empty())
                        subPrograms.append(ranges);
                    return WalkResult::Skip;
                }
                return WalkResult::Recurse;
            }, cudie());
        }
    };

    CuDieRanges(Dwfl_Module *mod = nullptr)
    {
        if (!mod)
            return;

        Dwarf_Die *die = nullptr;
        Dwarf_Addr bias = 0;
        while ((die = dwfl_module_nextcu(mod, die, &bias))) {
#ifndef NDEBUG
            qDebug() << hex << dwarf_diename(die) << bias << dwarf_dieoffset(die);
#endif
            CuDieRangeMapping cuDieMapping = {bias, {*die}};
            walkRanges([&cuDieMapping, bias](DwarfRange range) {
#ifndef NDEBUG
                qDebug() << hex << range.low << range.high << range.low + bias << range.high + bias;
#endif
                cuDieMapping.cuDieRanges.ranges.append({range.low + bias, range.high + bias});
                return true;
            }, die);
            if (!cuDieMapping.cuDieRanges.ranges.isEmpty()) {
                cuDieRanges.push_back(cuDieMapping);
            }
        }
    }

    CuDieRangeMapping *findCudie(Dwarf_Addr addr)
    {
        auto it = std::find_if(cuDieRanges.begin(), cuDieRanges.end(),
                               [addr](const CuDieRangeMapping &cuDieMapping) {
                                    return cuDieMapping.cuDieRanges.contains(addr);
                               });
        if (it == cuDieRanges.end())
            return nullptr;

        return &(*it);
    }

public:
    QVector<CuDieRangeMapping> cuDieRanges;
};
QT_BEGIN_NAMESPACE
Q_DECLARE_TYPEINFO(CuDieRanges, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(DwarfRange, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(CuDieRanges::DieRanges, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(CuDieRanges::CuDieRangeMapping, Q_MOVABLE_TYPE);
QT_END_NAMESPACE
