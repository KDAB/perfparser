import qbs
import qbs.FileInfo

QtcTool {
    name: "perfparser"

    Depends { name: "qtc" }

    Depends { name: "Qt.network" }

    Properties {
        cpp.includePaths: ["/usr/include/elfutils"]
        cpp.dynamicLibraries: ["dw", "elf"]
    }

    cpp.allowUnresolvedSymbols: true

    files: [
        "main.cpp",
        "perfattributes.cpp",
        "perfattributes.h",
        "perfheader.cpp",
        "perfheader.h",
        "perffilesection.cpp",
        "perffilesection.h",
        "perffeatures.cpp",
        "perffeatures.h",
        "perfdata.cpp",
        "perfdata.h",
        "perfunwind.cpp",
        "perfunwind.h",
        "perfregisterinfo.cpp",
        "perfregisterinfo.h",
        "perfstdin.cpp",
        "perfstdin.h",
        "perfsymboltable.cpp",
        "perfsymboltable.h",
        "perfelfmap.cpp",
        "perfelfmap.h",
        "perfkallsyms.cpp",
        "perfkallsyms.h"
    ]
}
