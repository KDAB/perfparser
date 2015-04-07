import qbs
import qbs.FileInfo

Project {
    name: "libcpu"
    property string gendisOutDir: FileInfo.joinPaths(project.buildDirectory, "gendis-stuff")
    references: [
        "i386_gendis/i386_gendis.qbs",
        "i386",
        "x86_64",
    ]
}
