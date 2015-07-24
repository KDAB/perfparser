TARGET = ebl_sparc
include(../backends.pri)

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
