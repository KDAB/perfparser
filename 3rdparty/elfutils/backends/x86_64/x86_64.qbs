import qbs

ElfUtilsBackend {
    arch: "x86_64"
    Depends { name: "x86_64 disasm" }
    additionalSources: ["cfi.c", "corenote.c", "initreg.c", "regs.c", "syscall.c"]
    Group {
        name: "more sources"
        prefix: "../"
        files: [
            "i386_auxv.c",
        ]
    }
}
