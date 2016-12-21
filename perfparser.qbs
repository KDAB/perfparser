import qbs

Project {
    name: "Perf Parser"
    condition: qbs.targetOS.contains("linux")

    property bool useSystemElfUtils: !qbs.toolchain.contains("gcc")
                                     || qbs.toolchain.contains("clang")
    property bool withAutotests: qbs.buildVariant === "debug"

    references: [
        "3rdparty/elfutils",
        "app",
        "tests",
    ]
}
