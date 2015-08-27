import qbs

Project {
    name: "Perf Parser"
    // condition: qbs.targetOS.contains("linux") TODO: Re-enable once qbs evaluation bug is fixed.

    property bool useSystemElfUtils: !qbs.toolchain.contains("gcc")
                                     || qbs.toolchain.contains("clang")

    references: [
        "3rdparty/elfutils",
        "app",
    ]
}
