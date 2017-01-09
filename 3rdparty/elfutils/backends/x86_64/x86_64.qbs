import qbs

ElfUtilsBackend {
    arch: "x86_64"
    Depends { name: "x86_64 disasm" }
    additionalSources: [
        "cfi.c",
        "corenote.c",
        "initreg.c",
        "regs.c",
        "retval.c",
        "syscall.c",
        "symbol.c",
        "unwind.c"
    ]
    Group {
        name: "more sources"
        prefix: "../"
        files: [
            "x32_corenote.c",
            "i386_auxv.c",
        ]
    }
}
