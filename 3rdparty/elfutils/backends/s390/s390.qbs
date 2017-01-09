import qbs

ElfUtilsBackend {
    arch: "s390"
    additionalSources: [
        "cfi.c", "corenote.c", "initreg.c", "regs.c", "retval.c", "symbol.c", "unwind.c"
    ]
    Group {
        name: "more sources"
        files: ["../s390x_corenote.c"]
    }
}
