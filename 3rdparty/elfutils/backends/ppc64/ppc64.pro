TARGET = ebl_ppc64
include(../backends.pri)

SOURCES += \
    ../ppc64_corenote.c \
    ../ppc64_init.c \
    ../ppc64_resolve_sym.c \
    ../ppc64_retval.c \
    ../ppc64_symbol.c \
    ../ppc_auxv.c \
    ../ppc_cfi.c \
    ../ppc_initreg.c \
    ../ppc_regs.c \
    ../ppc_syscall.c

HEADERS += \
    ../ppc64_reloc.def
