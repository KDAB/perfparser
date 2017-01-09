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
        prefix: FileInfo.joinPaths(product.sourceDirectory, "..") + '/' + arch + "_"
        files: [
            "init.c",
        ].concat(additionalSources)
    }
    Group {
        name: "headers"
        prefix: FileInfo.joinPaths(product.sourceDirectory, "..") + '/' + arch + "_"
        fileTags: ["hpp"]
        files: [
            "reloc.def"
        ]
    }
}
