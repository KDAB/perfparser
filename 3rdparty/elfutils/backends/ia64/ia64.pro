TEMPLATE = lib
include(../backends.pri)
TARGET = ../ebl_ia64

SOURCES += \
    ../ia64_init.c \
    ../ia64_regs.c \
    ../ia64_retval.c \
    ../ia64_symbol.c

HEADERS += \
    ../ia64_reloc.def
