import qbs

ElfUtilsBackend {
    arch: "tilegx"
    additionalSources: ["corenote.c", "regs.c"]
}
