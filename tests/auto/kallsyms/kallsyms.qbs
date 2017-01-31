import qbs

QtcAutotest {
    name: "Kallsyms Autotest"
    files: [
        "tst_perfkallsyms.cpp",
        "../../../app/perfkallsyms.cpp",
        "../../../app/perfkallsyms.h"
    ]
    cpp.includePaths: base.concat(["../../../app"])
}
