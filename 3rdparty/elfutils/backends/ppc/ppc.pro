TEMPLATE = lib
include(../backends.pri)
TARGET = ../../../../elfutils/ebl_ppc

SOURCES += \
    ../ppc_attrs.c \
    ../ppc_auxv.c \
    ../ppc_cfi.c \
    ../ppc_corenote.c \
    ../ppc_init.c \
    ../ppc_initreg.c \
    ../ppc_regs.c \
    ../ppc_retval.c \
    ../ppc_symbol.c \
    ../ppc_syscall.c

HEADERS += \
    ../ppc_reloc.def
