import qbs
import qbs.FileInfo

QtcTool {
    name: "perfparser"

    Depends { name: "dw"; condition: !project.useSystemElfUtils }
    Depends { name: "dwelf"; condition: !project.useSystemElfUtils }
    Depends { name: "dwfl"; condition: !project.useSystemElfUtils }
    Depends { name: "ebl"; condition: !project.useSystemElfUtils }
    Depends { name: "elf"; condition: !project.useSystemElfUtils }
    Depends { name: "elf32"; condition: !project.useSystemElfUtils }
    Depends { name: "elf64"; condition: !project.useSystemElfUtils }

    Depends { name: "qtc" }

    Depends { name: "Qt.network" }

    Properties {
        condition: project.useSystemElfUtils
        cpp.includePaths: ["/usr/include/elfutils"]
        cpp.dynamicLibraries: ["dw", "elf"]
    }

    cpp.allowUnresolvedSymbols: true
    cpp.rpaths: [
        "$ORIGIN/" + FileInfo.relativePath('/' + qtc.ide_libexec_path,
                                           '/' + qtc.ide_library_path) + '/',
    ]

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
