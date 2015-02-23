TEMPLATE = lib
include(../backends.pri)
TARGET = ../ebl_sparc

SOURCES += \
    ../sparc_auxv.c \
    ../sparc_corenote.c \
    ../sparc_init.c \
    ../sparc_regs.c \
    ../sparc_retval.c \
    ../sparc_symbol.c \
    ../sparc64_corenote.c

HEADERS += \
    ../sparc_reloc.def
