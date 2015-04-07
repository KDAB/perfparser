import qbs

ElfUtilsStaticLib {
    name: "dwelf"
    files: [
        "dwelf_dwarf_gnu_debugaltlink.c",
        "dwelf_elf_gnu_build_id.c",
        "dwelf_elf_gnu_debuglink.c",
        "libdwelf.h",
        "libdwelfP.h",
    ]
}
