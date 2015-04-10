TEMPLATE = lib
include(../backends.pri)
TARGET = ../../../../elfutils/ebl_alpha

SOURCES += \
    ../alpha_auxv.c \
    ../alpha_corenote.c \
    ../alpha_init.c \
    ../alpha_regs.c \
    ../alpha_retval.c \
    ../alpha_symbol.c

HEADERS += \
    ../alpha_reloc.def
