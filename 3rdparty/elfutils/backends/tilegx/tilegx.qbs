import qbs

ElfUtilsBackend {
    arch: "tilegx"
    additionalSources: ["corenote.c", "regs.c", "retval.c", "symbol.c"]
}
