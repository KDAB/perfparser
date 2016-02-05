import qbs

Project {
    name: "Perf Parser"
    condition: qbs.targetOS.contains("linux")

    property bool useSystemElfUtils: true

    references: [
        "3rdparty/elfutils",
        "app",
    ]
}
