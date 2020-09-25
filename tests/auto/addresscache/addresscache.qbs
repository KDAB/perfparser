import qbs

QtcAutotest {
    name: "AddressCache Autotest"
    files: [
        "tst_addresscache.cpp",
        "../../../app/perfelfmap.cpp",
        "../../../app/perfelfmap.h",
        "../../../app/perfaddresscache.cpp",
        "../../../app/perfaddresscache.h",
        "../../../app/perfdwarfdiecache.cpp",
        "../../../app/perfdwarfdiecache.h",
    ]
    cpp.includePaths: base.concat(["../../../app"]).concat(project.includePaths)
    cpp.libraryPaths: project.libPaths
    cpp.dynamicLibraries: ["dw", "elf"]
}
