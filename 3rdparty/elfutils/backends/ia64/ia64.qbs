import qbs

ElfUtilsBackend {
    arch: "ia64"
    additionalSources: ["regs.c", "retval.c", "symbol.c"]
}
