import qbs

ElfUtilsBackend {
    arch: "sparc"
    additionalSources: [
        "attrs.c", "auxv.c", "cfi.c", "corenote.c", "initreg.c", "regs.c", "retval.c", "symbol.c"
    ]
    Group {
        name: "more sources"
        files: ["../sparc64_corenote.c"]
    }
}
