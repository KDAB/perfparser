import qbs

Project {
    name: "elfutils"

    references: [
        "backends",
        "lib",
        "libasm",
        "libcpu",
        "libebl",
        "libelf",
        "libelf/elf32",
        "libelf/elf64",
        "libdw",
        "libdwelf",
        "libdwfl",
    ]

    Product {
        name: "elfutils text files"
        files: [
            "AUTHORS",
            "COPYING",
            "COPYING-GPLV2",
            "COPYING-LGPLV3",
            "THANKS",
        ]
    }

    qbsSearchPaths: ["qbs"]
}
