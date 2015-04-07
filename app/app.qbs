import qbs

QtApplication {
    name: "perfparser"
    consoleApplication: true

    Depends { name: "dw" }
    Depends { name: "dwelf" }
    Depends { name: "dwfl" }
    Depends { name: "ebl" }
    Depends { name: "elf" }
    Depends { name: "elf32" }
    Depends { name: "elf64" }

    Depends { name: "Qt.network" }

    cpp.allowUnresolvedSymbols: true
    cpp.cxxLanguageVersion: "c++11"

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
    ]
}
