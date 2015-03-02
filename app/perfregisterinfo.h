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

#ifndef PERFREGISTERINFO_H
#define PERFREGISTERINFO_H

#include <QtGlobal>

class PerfRegisterInfo
{
public:
    enum Architecture {
        ARCH_ARM = 0,
        ARCH_ARM64,
        ARCH_POWERPC,
        ARCH_S390,
        ARCH_SH,
        ARCH_SPARC,
        ARCH_X86,
        ARCH_INVALID
    };

    static const uint s_numAbis = 2; // maybe more for some archs?

    static const char *s_archNames[ARCH_INVALID];
    static const uint s_numRegisters[ARCH_INVALID][s_numAbis];

    // Translation table for converting perf register layout to dwarf register layout
    // This is specific to ABI as the different ABIs may have different numbers of registers.
    static const uint *s_perfToDwarf[ARCH_INVALID][s_numAbis];

    // location of IP register or equivalent in perf register layout for each arch/abi
    // This is not specific to ABI as perf makes sure IP is always in the same spot
    static const uint s_perfIp[ARCH_INVALID];
    // location of SP register or equivalent in perf register layout for each arch/abi
    static const uint s_perfSp[ARCH_INVALID];

    // location of LR register or equivalent in dwarf register layout for each arch/abi
    static const uint s_dwarfLr[ARCH_INVALID][s_numAbis];
    // location of IP register or equivalent in dwarf register layout for each arch/abi
    static const uint s_dwarfIp[ARCH_INVALID][s_numAbis];
};

#endif // PERFREGISTERINFO_H
