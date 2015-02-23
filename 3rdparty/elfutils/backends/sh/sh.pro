TEMPLATE = lib
include(../backends.pri)
TARGET = ../ebl_sh

SOURCES += \
    ../sh_corenote.c \
    ../sh_init.c \
    ../sh_regs.c \
    ../sh_retval.c \
    ../sh_symbol.c

HEADERS += \
    ../sh_reloc.def
