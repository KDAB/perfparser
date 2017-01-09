import qbs

ElfUtilsStaticLib {
    name: "dwelf"
    files: [
        "dwelf_dwarf_gnu_debugaltlink.c",
        "dwelf_elf_gnu_build_id.c",
        "dwelf_elf_gnu_debuglink.c",
        "dwelf_scn_gnu_compressed_size.c",
        "dwelf_strtab.c",
        "libdwelf.h",
        "libdwelfP.h",
    ]
}
