TARGET = ebl_tilegx
include(../backends.pri)

SOURCES += \
    ../tilegx_corenote.c \
    ../tilegx_init.c \
    ../tilegx_regs.c \
    ../tilegx_retval.c \
    ../tilegx_symbol.c

HEADERS += \
    ../tilegx_reloc.def
