#include "perfregisterinfo.h"

const char *PerfRegisterInfo::s_archNames[] = {
    "arm", "arm64", "powerpc", "s390", "sh", "sparc", "x86"
};

const uint PerfRegisterInfo::s_numRegisters[PerfRegisterInfo::s_numArchitectures][PerfRegisterInfo::s_numAbis] = {
    {16, 16},
    {33, 33},
    { 0,  0},
    { 0,  0},
    { 0,  0},
    { 0,  0},
    { 9, 17},
};

// Perf and Dwarf register layouts are the same for ARM and ARM64
static uint arm[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static uint arm64[] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
                       18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

// X86 is a mess
static uint x86[] = {0, 3, 2, 1, 7, 6, 4, 5, 8};
static uint x86_64[] = {0, 3, 2, 1, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23, 8};

static uint none[] = {};

const uint *PerfRegisterInfo::s_perfToDwarf[PerfRegisterInfo::s_numArchitectures][PerfRegisterInfo::s_numAbis] = {
    {arm,   arm   },
    {arm64, arm64 },
    {none,  none  },
    {none,  none  },
    {none,  none  },
    {none,  none  },
    {x86,   x86_64}
};

const uint PerfRegisterInfo::s_perfIp[s_numArchitectures] = {
    15, 32, 0xffff, 0xffff, 0xffff, 0xffff, 8
};

const uint PerfRegisterInfo::s_perfSp[s_numArchitectures] = {
    13, 31, 0xffff, 0xffff, 0xffff, 0xffff, 7
};
