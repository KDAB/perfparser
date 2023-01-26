import qbs

QtcAutotest {
    name: "finddebugsym Autotest"
    files: [
        "tst_finddebugsym.cpp",
        "../../../app/demangler.cpp",
        "../../../app/demangler.h",
        "../../../app/perfaddresscache.cpp",
        "../../../app/perfaddresscache.h",
        "../../../app/perfattributes.cpp",
        "../../../app/perfattributes.h",
        "../../../app/perfdata.cpp",
        "../../../app/perfdata.h",
        "../../../app/perfdwarfdiecache.cpp",
        "../../../app/perfdwarfdiecache.h",
        "../../../app/perfelfmap.cpp",
        "../../../app/perfelfmap.h",
        "../../../app/perffeatures.cpp",
        "../../../app/perffeatures.h",
        "../../../app/perffilesection.cpp",
        "../../../app/perffilesection.h",
        "../../../app/perfheader.cpp",
        "../../../app/perfheader.h",
        "../../../app/perfkallsyms.cpp",
        "../../../app/perfkallsyms.h",
        "../../../app/perfregisterinfo.cpp",
        "../../../app/perfregisterinfo.h",
        "../../../app/perfsymboltable.cpp",
        "../../../app/perfsymboltable.h",
        "../../../app/perftracingdata.cpp",
        "../../../app/perftracingdata.h",
        "../../../app/perfunwind.cpp",
        "../../../app/perfunwind.h",
    ]
    cpp.includePaths: base.concat(["../../../app"]).concat(project.includePaths)
    cpp.libraryPaths: project.libPaths
    cpp.dynamicLibraries: ["dw", "elf"]
}
