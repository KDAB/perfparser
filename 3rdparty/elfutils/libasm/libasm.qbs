import qbs

ElfUtilsDynamicLib {
    name: "asm"
    Depends { name: "ebl" }
    Depends { name: "elf" }
    cpp.allowUnresolvedSymbols: true
    files: [
        "asm_abort.c",
        "asm_addint8.c",
        "asm_addint16.c",
        "asm_addint32.c",
        "asm_addint64.c",
        "asm_addsleb128.c",
        "asm_addstrz.c",
        "asm_adduint8.c",
        "asm_adduint16.c",
        "asm_adduint32.c",
        "asm_adduint64.c",
        "asm_adduleb128.c",
        "asm_align.c",
        "asm_begin.c",
        "asm_end.c",
        "asm_error.c",
        "asm_fill.c",
        "asm_getelf.c",
        "asm_newabssym.c",
        "asm_newcomsym.c",
        "asm_newscn_ingrp.c",
        "asm_newscn.c",
        "asm_newscngrp.c",
        "asm_newsubscn.c",
        "asm_newsym.c",
        "asm_scngrp_newsignature.c",
        "disasm_begin.c",
        "disasm_cb.c",
        "disasm_end.c",
        "disasm_str.c",
        "libasm.h",
        "libasmP.h",
        "symbolhash.c",
        "symbolhash.h",
    ]
}
