import qbs

ElfUtilsProduct {
    type: ["staticlibrary"]
    cpp.positionIndependentCode: true

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: [product.sourceDirectory]
    }
}
