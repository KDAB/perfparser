TEMPLATE = lib
include(../backends.pri)
TARGET = ../../../../elfutils/ebl_ppc64

SOURCES += \
    ../ppc64_corenote.c \
    ../ppc64_init.c \
    ../ppc64_resolve_sym.c \
    ../ppc64_retval.c \
    ../ppc64_symbol.c

HEADERS += \
    ../ppc64_reloc.def
