import qbs
import qbs.FileInfo

Product {
    Depends { name: "cpp" }
    version: "0.163"
    cpp.cFlags: ["-std=gnu99"]
    cpp.defines: ["HAVE_CONFIG_H", "_GNU_SOURCE"]
    property string elfUtilsDir: FileInfo.joinPaths(path, "..", "..")
    cpp.includePaths: [
        elfUtilsDir,
        FileInfo.joinPaths(elfUtilsDir, "lib"),
        FileInfo.joinPaths(elfUtilsDir, "libasm"),
        FileInfo.joinPaths(elfUtilsDir, "libdw"),
        FileInfo.joinPaths(elfUtilsDir, "libdwelf"),
        FileInfo.joinPaths(elfUtilsDir, "libdwfl"),
        FileInfo.joinPaths(elfUtilsDir, "libebl"),
        FileInfo.joinPaths(elfUtilsDir, "libelf"),
        product.sourceDirectory,
    ]
}
