import qbs
import qbs.FileInfo

QtcTool {
    name: "perfparser"

    Depends { name: "qtc" }

    Depends { name: "Qt.network" }

    cpp.defines: base.filter(function(def) { return def != "QT_RESTRICTED_CAST_FROM_ASCII"; })
    cpp.includePaths: ["/usr/include/elfutils"]
    cpp.dynamicLibraries: ["dw", "elf"]

    files: [
        "main.cpp",
        "perfaddresscache.cpp",
        "perfaddresscache.h",
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
        "perfkallsyms.h",
        "perftracingdata.cpp",
        "perftracingdata.h",
    ]
}
