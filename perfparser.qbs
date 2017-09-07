import qbs

Project {
    name: "Perf Parser"
    condition: qbs.targetOS.contains("linux")

    property bool withAutotests: qbs.buildVariant === "debug"

    references: [
        "app",
        "tests",
    ]
}
