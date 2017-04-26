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

#include "perfregisterinfo.h"

const char *PerfRegisterInfo::s_archNames[] = {
    "arm", "aarch64", "powerpc", "s390", "sh", "sparc", "x86"
};

const uint PerfRegisterInfo::s_numRegisters[PerfRegisterInfo::ARCH_INVALID][PerfRegisterInfo::s_numAbis] = {
    {16, 16},
    {33, 33},
    { 0,  0},
    { 0,  0},
    { 0,  0},
    { 0,  0},
    { 9, 17},
};

const uint PerfRegisterInfo::s_wordWidth[PerfRegisterInfo::ARCH_INVALID][PerfRegisterInfo::s_numAbis] = {
    {4, 4},
    {8, 8},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {4, 8},
};

// Perf and Dwarf register layouts are the same for ARM and ARM64
static uint arm[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static uint aarch64[] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
                         18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

// X86 is a mess
static uint x86[] = {0, 2, 3, 1, 7, 6, 4, 5, 8};
static uint x86_64[] = {0, 3, 2, 1, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23, 8};

static uint none[] = {0};

const uint *PerfRegisterInfo::s_perfToDwarf[PerfRegisterInfo::ARCH_INVALID][PerfRegisterInfo::s_numAbis] = {
    {arm,     arm    },
    {aarch64, aarch64},
    {none,    none   },
    {none,    none   },
    {none,    none   },
    {none,    none   },
    {x86,     x86_64 }
};

const uint PerfRegisterInfo::s_perfIp[ARCH_INVALID] = {
    15, 32, 0xffff, 0xffff, 0xffff, 0xffff, 8
};

const uint PerfRegisterInfo::s_perfSp[ARCH_INVALID] = {
    13, 31, 0xffff, 0xffff, 0xffff, 0xffff, 7
};

const uint PerfRegisterInfo::s_dwarfLr[ARCH_INVALID][s_numAbis] = {
    {14, 14},
    {30, 30},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff}
};

const uint PerfRegisterInfo::s_dwarfIp[ARCH_INVALID][s_numAbis] = {
    {15, 15},
    {32, 32},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {8, 16}
};

const uint PerfRegisterInfo::s_dummyRegisters[ARCH_INVALID][2] = {
    {0, 0},
    {72, 80},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}
};

const PerfRegisterInfo::Architecture PerfRegisterInfo::s_defaultArchitecture = {
#if defined(__aarch64__)
    PerfRegisterInfo::ARCH_AARCH64
#elif defined(__arm__)
    PerfRegisterInfo::ARCH_ARM
#elif defined(__powerpc__)
    PerfRegisterInfo::ARCH_POWERPC
#elif defined(__s390__)
    PerfRegisterInfo::ARCH_S390
#elif defined(__sh__)
    PerfRegisterInfo::ARCH_SH
#elif defined(__sparc__)
    PerfRegisterInfo::ARCH_SPARC
#elif defined(__i386__) || defined(__x86_64__)
    PerfRegisterInfo::ARCH_X86
#else
    PerfRegisterInfo::ARCH_INVALID
#endif
};
