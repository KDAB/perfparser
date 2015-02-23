TEMPLATE = lib
include(../backends.pri)
TARGET = ../ebl_x86_64

SOURCES += \
    ../x86_64_cfi.c \
    ../x86_64_corenote.c \
    ../x86_64_init.c \
    ../x86_64_initreg.c \
    ../x86_64_regs.c \
    ../x86_64_retval.c \
    ../x86_64_symbol.c \
    ../x86_64_syscall.c \
    ../x86_corenote.c

HEADERS += \
    ../x86_64_reloc.def
