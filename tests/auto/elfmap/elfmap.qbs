import qbs

QtcAutotest {
    name: "Elfmap Autotest"
    files: [
        "tst_elfmap.cpp",
        "../../../app/perfelfmap.cpp",
        "../../../app/perfelfmap.h"
    ]
    cpp.includePaths: base.concat(["../../../app"])
}
