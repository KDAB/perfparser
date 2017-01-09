import qbs

ElfUtilsStaticLib {
    name: "elf32"
    Group {
        prefix: "../"
        files: [
            "elf32_checksum.c",
            "elf32_fsize.c",
            "elf32_getchdr.c",
            "elf32_getehdr.c",
            "elf32_getphdr.c",
            "elf32_getshdr.c",
            "elf32_newehdr.c",
            "elf32_newphdr.c",
            "elf32_offscn.c",
            "elf32_updatefile.c",
            "elf32_updatenull.c",
            "elf32_xlatetof.c",
            "elf32_xlatetom.c",
        ]
    }
}
