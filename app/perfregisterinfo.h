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

#include <QtGlobal>

class PerfRegisterInfo
{
public:
    enum Architecture {
        ARCH_ARM = 0,
        ARCH_AARCH64,
        ARCH_POWERPC,
        ARCH_S390,
        ARCH_SH,
        ARCH_SPARC,
        ARCH_X86,
        ARCH_MIPS,
        ARCH_INVALID
    };

    static const int s_numAbis = 2; // maybe more for some archs?

    static Architecture archByName(const QByteArray &name);
    static const int s_numRegisters[ARCH_INVALID][s_numAbis];
    static const int s_wordWidth[ARCH_INVALID][s_numAbis];

    // Translation table for converting perf register layout to dwarf register layout
    // This is specific to ABI as the different ABIs may have different numbers of registers.
    static const int *s_perfToDwarf[ARCH_INVALID][s_numAbis];

    // location of IP register or equivalent in perf register layout for each arch/abi
    // This is not specific to ABI as perf makes sure IP is always in the same spot
    static const int s_perfIp[ARCH_INVALID];
    // location of SP register or equivalent in perf register layout for each arch/abi
    static const int s_perfSp[ARCH_INVALID];

    // location of LR register or equivalent in dwarf register layout for each arch/abi
    static const int s_dwarfLr[ARCH_INVALID][s_numAbis];
    // location of IP register or equivalent in dwarf register layout for each arch/abi
    static const int s_dwarfIp[ARCH_INVALID][s_numAbis];

    // ranges of registers expected by libdw, but not provided by perf
    static const int s_dummyRegisters[ARCH_INVALID][2];

    // default architecture for the system which was used for compilation
    static const char *defaultArchitecture();
};
