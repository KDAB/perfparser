import qbs

QtcAutotest {
    name: "PerfData Autotest"

    cpp.includePaths: ["/usr/include/elfutils", "../../../app", "../shared"]
    cpp.dynamicLibraries: ["dw", "elf"]

    files: [
        "perfdata.qrc",
        "tst_perfdata.cpp",
        "../shared/perfparsertestclient.cpp",
        "../shared/perfparsertestclient.h",
        "../../../app/perfaddresscache.cpp",
        "../../../app/perfaddresscache.h",
        "../../../app/perfattributes.cpp",
        "../../../app/perfattributes.h",
        "../../../app/perfdata.cpp",
        "../../../app/perfdata.h",
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
}
