import qbs

ElfUtilsBackend {
    arch: "aarch64"
    additionalSources: ["cfi.c", "corenote.c", "initreg.c", "regs.c"]
}
