import qbs

ElfUtilsProduct {
    type: ["dynamiclibrary", "dynamiclibrary_symlink"]
    property bool isBackend: false

    Depends { name: "qtc" }

    Group {
        fileTagsFilter: product.type
        qbs.install: true
        qbs.installDir: qtc.ide_library_path + (isBackend ? "/elfutils" : "")
    }
    cpp.rpaths: [
        "$ORIGIN/",
        isBackend ? "$ORIGIN/../" : "$ORIGIN/elfutils/",
    ]
}
