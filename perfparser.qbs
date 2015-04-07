import qbs

Project {
    name: "Perf Parser"
    references: [
        "3rdparty/elfutils",
        "app",
    ]
}
