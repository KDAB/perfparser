import qbs

ElfUtilsBackend {
    arch: "ppc64"
    additionalSources: ["corenote.c", "resolve_sym.c", "retval.c", "symbol.c"]

    Group {
        name: "ppcSources"
        prefix:  "../ppc_"
        files: ["auxv.c", "cfi.c", "initreg.c", "regs.c", "syscall.c"]
    }
}
