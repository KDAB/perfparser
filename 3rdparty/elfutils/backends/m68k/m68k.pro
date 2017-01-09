TARGET = ebl_m68k
include(../backends.pri)

SOURCES += \
    ../m68k_corenote.c \
    ../m68k_init.c \
    ../m68k_regs.c \
    ../m68k_retval.c \
    ../m68k_symbol.c

HEADERS += \
    ../m68k_reloc.def
