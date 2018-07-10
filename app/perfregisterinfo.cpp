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

#include <QByteArray>
#include <QRegularExpression>

const int PerfRegisterInfo::s_numRegisters[PerfRegisterInfo::ARCH_INVALID][PerfRegisterInfo::s_numAbis] = {
    {16, 16},
    {33, 33},
    { 0,  0},
    { 0,  0},
    { 0,  0},
    { 0,  0},
    { 9, 17},
};

const int PerfRegisterInfo::s_wordWidth[PerfRegisterInfo::ARCH_INVALID][PerfRegisterInfo::s_numAbis] = {
    {4, 4},
    {8, 8},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {4, 8},
};

// Perf and Dwarf register layouts are the same for ARM and ARM64
static int arm[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static int aarch64[] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
                        18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

// X86 is a mess
static int x86[] = {0, 2, 3, 1, 7, 6, 4, 5, 8};
static int x86_64[] = {0, 3, 2, 1, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23, 8};

static int mips[] = { 32,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
                        18, 19, 20, 21, 22, 23, 24, 25, 28, 29, 30, 31};

static int none[] = {0};

const int *PerfRegisterInfo::s_perfToDwarf[PerfRegisterInfo::ARCH_INVALID][PerfRegisterInfo::s_numAbis] = {
    {arm,     arm    },
    {aarch64, aarch64},
    {none,    none   },
    {none,    none   },
    {none,    none   },
    {none,    none   },
    {x86,     x86_64 },
    {mips,    mips   },
};

const int PerfRegisterInfo::s_perfIp[ARCH_INVALID] = {
    15, 32, 0xffff, 0xffff, 0xffff, 0xffff, 8
};

const int PerfRegisterInfo::s_perfSp[ARCH_INVALID] = {
    13, 31, 0xffff, 0xffff, 0xffff, 0xffff, 7
};

const int PerfRegisterInfo::s_dwarfLr[ARCH_INVALID][s_numAbis] = {
    {14, 14},
    {30, 30},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff}
};

const int PerfRegisterInfo::s_dwarfIp[ARCH_INVALID][s_numAbis] = {
    {15, 15},
    {32, 32},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {0xffff, 0xffff},
    {8, 16}
};

const int PerfRegisterInfo::s_dummyRegisters[ARCH_INVALID][2] = {
    {0, 0},
    {72, 80},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}
};

const char *PerfRegisterInfo::defaultArchitecture()
{
#if defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#elif defined(__powerpc__)
    return "powerpc";
#elif defined(__s390__)
    return "s390";
#elif defined(__sh__)
    return "sh";
#elif defined(__sparc__)
    return "sparc";
#elif defined(__i386__) || defined(__x86_64__)
    return "x86";
#else
    return "";
#endif
};

PerfRegisterInfo::Architecture PerfRegisterInfo::archByName(const QByteArray &name)
{
    if (name == "aarch64" || name == "arm64")
        return ARCH_AARCH64;

    if (name.startsWith("arm"))
        return ARCH_ARM;

    if (name.startsWith("powerpc"))
        return ARCH_POWERPC;

    if (name.startsWith("s390"))
        return ARCH_S390;

    if (name.startsWith("sh"))
        return ARCH_SH;

    if (name.startsWith("sparc"))
        return ARCH_SPARC;

    if (name.startsWith("x86")
            || QRegularExpression("^i[3-7]86$").match(name).hasMatch()
            || name == "amd64")
        return ARCH_X86;

    if (name.startsWith("mips"))
        return ARCH_MIPS;

    return ARCH_INVALID;
}
