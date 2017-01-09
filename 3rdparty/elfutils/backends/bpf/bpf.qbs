import qbs

ElfUtilsBackend {
    arch: "bpf"
    additionalSources: ["regs.c"]
}
