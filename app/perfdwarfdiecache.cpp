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

#include <time.h>

#include "perfdwarfdiecache.h"
#include "perfeucompat.h"

#include <dwarf.h>

#include <QLibrary>
#include <QDebug>

namespace {
bool rustc_demangle(const char *symbol, char *buffer, size_t bufferSize)
{
    using demangler_t = int (*) (const char*, char *, size_t);
    static const auto demangler = []() -> demangler_t {
        QLibrary lib(QStringLiteral("rustc_demangle"));
        if (!lib.load()) {
            qDebug() << "failed to load rustc_demangle library, rust demangling is support not available."
                     << lib.errorString();
            return nullptr;
        }
        const auto rawSymbol = lib.resolve("rustc_demangle");
        if (!rawSymbol) {
            qDebug() << "failed to resolve rustc_demangle function in library"
                     << lib.fileName() << lib.errorString();
            return nullptr;
        }
        return reinterpret_cast<demangler_t>(rawSymbol);
    }();

    if (demangler)
        return demangler(symbol, buffer, bufferSize);
    else
        return false;
}

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

// see libdw_visit_scopes.c in elfutils
bool mayHaveScopes(Dwarf_Die *die)
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

bool dieContainsAddress(Dwarf_Die *die, Dwarf_Addr address)
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
}

const char *linkageName(Dwarf_Die *die)
{
    Dwarf_Attribute attr;
    Dwarf_Attribute *result = dwarf_attr_integrate(die, DW_AT_MIPS_linkage_name, &attr);
    if (!result)
        result = dwarf_attr_integrate(die, DW_AT_linkage_name, &attr);

    return result ? dwarf_formstring(result) : nullptr;
}

Dwarf_Die *specificationDie(Dwarf_Die *die, Dwarf_Die *dieMem)
{
    Dwarf_Attribute attr;
    if (dwarf_attr_integrate(die, DW_AT_specification, &attr))
        return dwarf_formref_die(&attr, dieMem);
    return nullptr;
}

/// prepend the names of all scopes that reference the @p die to @p name
void prependScopeNames(QByteArray &name, Dwarf_Die *die, QHash<Dwarf_Off, QByteArray> &cache)
{
    Dwarf_Die dieMem;
    Dwarf_Die *scopes = nullptr;
    auto nscopes = dwarf_getscopes_die(die, &scopes);

    struct ScopesToCache
    {
        Dwarf_Off offset;
        int trailing;
    };
    QVector<ScopesToCache> cacheOps;

    // skip scope for the die itself at the start and the compile unit DIE at end
    for (int i = 1; i < nscopes - 1; ++i) {
        auto scope = scopes + i;

        const auto scopeOffset = dwarf_dieoffset(scope);

        auto it = cache.find(scopeOffset);
        if (it != cache.end()) {
            name.prepend(*it);
            // we can stop, cached names are always fully qualified
            break;
        }

        if (auto scopeLinkageName = linkageName(scope)) {
            // prepend the fully qualified linkage name
            name.prepend("::");
            cacheOps.append({scopeOffset, int(name.size())});
            // we have to demangle the scope linkage name, otherwise we get a
            // mish-mash of mangled and non-mangled names
            name.prepend(demangle(scopeLinkageName));
            // we can stop now, the scope is fully qualified
            break;
        }

        if (auto scopeName = dwarf_diename(scope)) {
            // prepend this scope's name, e.g. the class or namespace name
            name.prepend("::");
            cacheOps.append({scopeOffset, int(name.size())});
            name.prepend(scopeName);
        }

        if (auto specification = specificationDie(scope, &dieMem)) {
            eu_compat_free(scopes);
            scopes = nullptr;
            cacheOps.append({scopeOffset, int(name.size())});
            cacheOps.append({dwarf_dieoffset(specification), int(name.size())});
            // follow the scope's specification DIE instead
            prependScopeNames(name, specification, cache);
            break;
        }
    }

    for (const auto &cacheOp : cacheOps)
        cache[cacheOp.offset] = name.mid(0, name.size() - cacheOp.trailing);

    eu_compat_free(scopes);
}

bool operator==(const Dwarf_Die &lhs, const Dwarf_Die &rhs)
{
    return lhs.addr == rhs.addr && lhs.cu == rhs.cu && lhs.abbrev == rhs.abbrev;
}

QByteArray qualifiedDieName(Dwarf_Die *die, QHash<Dwarf_Off, QByteArray> &cache)
{
    // linkage names are fully qualified, meaning we can stop early then
    if (auto name = linkageName(die))
        return name;

    // otherwise do a more complex lookup that includes namespaces and other context information
    // this is important for inlined subroutines such as lambdas or std:: algorithms
    QByteArray name = dwarf_diename(die);

    // use the specification DIE which is within the DW_TAG_namespace
    Dwarf_Die dieMem;
    if (auto specification = specificationDie(die, &dieMem))
        die = specification;

    prependScopeNames(name, die, cache);

    return name;
}

QByteArray demangle(const QByteArray &mangledName)
{
    if (mangledName.length() < 3) {
        return mangledName;
    } else {
        static size_t demangleBufferLength = 1024;
        static char *demangleBuffer = reinterpret_cast<char *>(eu_compat_malloc(demangleBufferLength));

        if (rustc_demangle(mangledName.constData(), demangleBuffer, demangleBufferLength))
            return demangleBuffer;

        // Require GNU v3 ABI by the "_Z" prefix.
        if (mangledName[0] == '_' && mangledName[1] == 'Z') {
            int status = -1;
            char *dsymname = eu_compat_demangle(mangledName.constData(), demangleBuffer, &demangleBufferLength,
                                            &status);
            if (status == 0)
                return demangleBuffer = dsymname;
        }
    }
    return mangledName;
}

QVector<Dwarf_Die> findInlineScopes(Dwarf_Die *subprogram, Dwarf_Addr offset)
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

SubProgramDie::SubProgramDie(Dwarf_Die die)
    : m_ranges{die, {}}
{
    walkRanges([this](DwarfRange range) {
        m_ranges.ranges.append(range);
        return true;
    }, &die);
}

SubProgramDie::~SubProgramDie() = default;

CuDieRangeMapping::CuDieRangeMapping(Dwarf_Die cudie, Dwarf_Addr bias)
    : m_bias{bias}
    , m_cuDieRanges{cudie, {}}
{
    walkRanges([this, bias](DwarfRange range) {
        m_cuDieRanges.ranges.append({range.low + bias, range.high + bias});
        return true;
    }, &cudie);
}

CuDieRangeMapping::~CuDieRangeMapping() = default;

SubProgramDie *CuDieRangeMapping::findSubprogramDie(Dwarf_Addr offset)
{
    if (m_subPrograms.isEmpty())
        addSubprograms();

    auto it = std::find_if(m_subPrograms.begin(), m_subPrograms.end(),
                        [offset](const SubProgramDie &program) {
                                return program.contains(offset);
                        });
    if (it == m_subPrograms.end())
        return nullptr;

    return &(*it);
}

void CuDieRangeMapping::addSubprograms()
{
    walkDieTree([this](Dwarf_Die *die) {
        if (!mayHaveScopes(die))
            return WalkResult::Skip;

        if (dwarf_tag(die) == DW_TAG_subprogram) {
            SubProgramDie program(*die);
            if (!program.isEmpty())
                m_subPrograms.append(program);

            return WalkResult::Skip;
        }
        return WalkResult::Recurse;
    }, cudie());
}

QByteArray CuDieRangeMapping::dieName(Dwarf_Die *die)
{
    auto &name = m_dieNameCache[dwarf_dieoffset(die)];
    if (name.isEmpty())
        name = demangle(qualifiedDieName(die, m_dieNameCache));

    return name;
}

PerfDwarfDieCache::PerfDwarfDieCache(Dwfl_Module *mod)
{
    if (!mod)
        return;

    Dwarf_Die *die = nullptr;
    Dwarf_Addr bias = 0;
    while ((die = dwfl_module_nextcu(mod, die, &bias))) {
        CuDieRangeMapping cuDieMapping(*die, bias);
        if (!cuDieMapping.isEmpty())
            m_cuDieRanges.push_back(cuDieMapping);
    }
}

PerfDwarfDieCache::~PerfDwarfDieCache() = default;

CuDieRangeMapping *PerfDwarfDieCache::findCuDie(Dwarf_Addr addr)
{
    auto it = std::find_if(m_cuDieRanges.begin(), m_cuDieRanges.end(),
                            [addr](const CuDieRangeMapping &cuDieMapping) {
                                return cuDieMapping.contains(addr);
                            });
    if (it == m_cuDieRanges.end())
        return nullptr;

    return &(*it);
}
