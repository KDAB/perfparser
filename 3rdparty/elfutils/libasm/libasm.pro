TARGET = asm

include(../dynamic.pri)
include(../libebl/eblheaders.pri)
include(asmheaders.pri)

SOURCES += \
    $$PWD/asm_abort.c \
    $$PWD/asm_addint8.c \
    $$PWD/asm_addint16.c \
    $$PWD/asm_addint32.c \
    $$PWD/asm_addint64.c \
    $$PWD/asm_addsleb128.c \
    $$PWD/asm_addstrz.c \
    $$PWD/asm_adduint8.c \
    $$PWD/asm_adduint16.c \
    $$PWD/asm_adduint32.c \
    $$PWD/asm_adduint64.c \
    $$PWD/asm_adduleb128.c \
    $$PWD/asm_align.c \
    $$PWD/asm_begin.c \
    $$PWD/asm_end.c \
    $$PWD/asm_error.c \
    $$PWD/asm_fill.c \
    $$PWD/asm_getelf.c \
    $$PWD/asm_newabssym.c \
    $$PWD/asm_newcomsym.c \
    $$PWD/asm_newscn_ingrp.c \
    $$PWD/asm_newscn.c \
    $$PWD/asm_newscngrp.c \
    $$PWD/asm_newsubscn.c \
    $$PWD/asm_newsym.c \
    $$PWD/asm_scngrp_newsignature.c \
    $$PWD/disasm_begin.c \
    $$PWD/disasm_cb.c \
    $$PWD/disasm_end.c \
    $$PWD/disasm_str.c \
    $$PWD/symbolhash.c
