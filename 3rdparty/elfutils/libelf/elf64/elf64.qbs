import qbs

ElfUtilsStaticLib {
    name: "elf64"
    Group {
        prefix: "../"
        files: [
            "elf64_checksum.c",
            "elf64_fsize.c",
            "elf64_getehdr.c",
            "elf64_getphdr.c",
            "elf64_getshdr.c",
            "elf64_newehdr.c",
            "elf64_newphdr.c",
            "elf64_offscn.c",
            "elf64_updatefile.c",
            "elf64_updatenull.c",
            "elf64_xlatetof.c",
            "elf64_xlatetom.c",
        ]
    }
}
