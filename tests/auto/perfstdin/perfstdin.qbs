import qbs

QtcAutotest {
    name: "PerfStdin Autotest"
    files: [
        "tst_perfstdin.cpp",
        "../../../app/perfstdin.cpp",
        "../../../app/perfstdin.h"
    ]
    cpp.includePaths: base.concat(["../../../app"])
}
