import qbs
import qbs.FileInfo

ElfUtilsStaticLib {
    property string arch
    destinationDirectory: project.gendisOutDir
    name: arch + " disasm"
    Depends { name: arch + " disasm header" }
    Depends { name: arch + " mnemonics" }
    cpp.includePaths: base.concat([FileInfo.joinPaths(product.sourceDirectory, "..")])
    files: ["../" + arch + "_disasm.c"]
}
