import qbs

ElfUtilsStaticLib {
    name: "eu"
    files: [
        "color.c",
        "crc32_file.c",
        "crc32.c",
        "md5.c",
        "next_prime.c",
        "sha1.c",
        "version.c",
        "xmalloc.c",
        "xstrdup.c",
        "xstrndup.c",
    ]
}
