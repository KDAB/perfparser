import qbs

ElfUtilsBackend {
    arch: "m68k"
    additionalSources: ["corenote.c", "regs.c", "retval.c", "symbol.c"]
}
