import qbs

ElfUtilsProduct {
    type: ["dynamiclibrary", "dynamiclibrary_symlink"]
    property bool isBackend: false

    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: project.ide_library_path + (isBackend ? "/elfutils" : "")
    }
    cpp.rpaths: [
        "$ORIGIN/",
        isBackend ? "$ORIGIN/../" : "$ORIGIN/elfutils/",
    ]
}
