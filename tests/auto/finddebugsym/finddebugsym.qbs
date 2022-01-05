import qbs

QtcAutotest {
    name: "finddebugsym Autotest"
    files: [
        "tst_finddebugsym.cpp",
        "../../../app/perfkallsyms.cpp",
        "../../../app/perfkallsyms.h"
    ]
    cpp.includePaths: base.concat(["../../../app"])
}
