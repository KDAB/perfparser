import qbs

ElfUtilsBackend {
    arch: "i386"
    Depends { name: "i386 disasm" }
    additionalSources: [
        "auxv.c", "cfi.c", "corenote.c", "initreg.c", "regs.c", "retval.c", "symbol.c", "syscall.c"
    ]
}
