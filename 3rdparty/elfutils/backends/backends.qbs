import qbs

Project {
    name: "backends"
    references: [
        "aarch64",
        "alpha",
        "arm",
        "i386",
        "ia64",
        "ppc",
        "ppc64",
        "s390",
        "sh",
        "sparc",
        "tilegx",
        "x86_64",
    ]
}
