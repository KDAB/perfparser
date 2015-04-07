import qbs

ElfUtilsBackend {
    arch: "ppc64"
    additionalSources: ["corenote.c", "resolve_sym.c"]
}
