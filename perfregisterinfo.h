#ifndef PERFREGISTERINFO_H
#define PERFREGISTERINFO_H

#include <QObject>

class PerfRegisterInfo
{
public:
    PerfRegisterInfo();

    static const uint s_numArchitectures = 7;
    static const uint s_numAbis = 2; // maybe more for some archs?

    static const char *s_archNames[s_numArchitectures];
    static const uint s_numRegisters[s_numArchitectures][s_numAbis];

    // Translation table for converting perf register layout to dwarf register layout
    // This is specific to ABI as the different ABIs may have different numbers of registers.
    static const uint *s_perfToDwarf[s_numArchitectures][s_numAbis];

    // location of IP register or equivalent in perf register layout for each arch/abi
    // This is not specific to ABI as perf makes sure IP is always in the same spot
    static const uint s_perfIp[s_numArchitectures];
    // location of SP register or equivalent in perf register layout for each arch/abi
    static const uint s_perfSp[s_numArchitectures];
};

#endif // PERFREGISTERINFO_H
