import qbs

ElfUtilsBackend {
    arch: "sparc"
    additionalSources: ["auxv.c", "corenote.c", "regs.c"]
    Group {
        name: "more sources"
        files: ["../sparc64_corenote.c"]
    }
}
