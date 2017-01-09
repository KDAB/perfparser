import qbs

ElfUtilsBackend {
    arch: "alpha"
    additionalSources: ["auxv.c", "corenote.c", "regs.c", "retval.c", "symbol.c"]
}
