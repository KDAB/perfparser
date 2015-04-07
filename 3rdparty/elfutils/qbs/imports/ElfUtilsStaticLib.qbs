import qbs

ElfUtilsProduct {
    type: ["staticlibrary"]

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [product.sourceDirectory]
    }
}
