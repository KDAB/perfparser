import qbs.Environment
import qbs.FileInfo

Project {
    name: "Perf Parser"
    condition: qbs.targetOS.contains("linux")

    cpp.defines: base.concat(["QT_NO_FOREACH"])

    property bool withAutotests: qbs.buildVariant === "debug"

    property string installBase: Environment.getEnv("ELFUTILS_INSTALL_DIR")
    property stringList includePaths: installBase
        ? [FileInfo.joinPaths(installBase, "include"),
           FileInfo.joinPaths(installBase, "include", "elfutils")]
        : "/usr/include/elfutils"
    property stringList libPaths: installBase ? [FileInfo.joinPaths(installBase, "lib")] : []

    references: [
        "app",
        "tests",
    ]
}
