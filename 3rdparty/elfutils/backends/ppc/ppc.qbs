import qbs

ElfUtilsBackend {
    arch: "ppc"
    additionalSources: [
        "attrs.c",
        "auxv.c",
        "cfi.c",
        "corenote.c",
        "initreg.c",
        "regs.c",
        "retval.c",
        "symbol.c",
        "syscall.c"
    ]
}
