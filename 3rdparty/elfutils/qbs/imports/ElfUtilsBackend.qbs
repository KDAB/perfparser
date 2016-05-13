import qbs
import qbs.FileInfo

ElfUtilsDynamicLib {
    property string arch
    name: "ebl_" + arch
    isBackend: true
    property stringList additionalSources: []
    property string backendDir: FileInfo.joinPaths(product.sourceDirectory, "..")
    Depends { name: "dw" }
    Depends { name: "elf" }
    cpp.includePaths: base.concat([backendDir])
    cpp.allowUnresolvedSymbols: true
    Group {
        name: "sources"
        prefix: "../" + arch + "_"
        files: [
            "init.c",
            "retval.c",
            "symbol.c",
        ].concat(additionalSources)
    }
    Group {
        name: "headers"
        fileTags: ["hpp"]
        files: [
            "../" + arch + "_reloc.def"
        ]
    }
}
