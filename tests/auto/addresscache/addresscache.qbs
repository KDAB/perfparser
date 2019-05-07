import qbs

QtcAutotest {
    name: "AddressCache Autotest"
    files: [
        "tst_addresscache.cpp",
        "../../../app/perfelfmap.cpp",
        "../../../app/perfelfmap.h",
        "../../../app/perfaddresscache.cpp",
        "../../../app/perfaddresscache.h"
    ]
    cpp.includePaths: base.concat(["../../../app"])
}
