import qbs

QtcAutotest {
    name: "Kallsyms Autotest"
    files: [
        "tst_kallsyms.cpp",
        "../../../app/perfkallsyms.cpp",
        "../../../app/perfkallsyms.h"
    ]
    cpp.includePaths: base.concat(["../../../app"])
}
