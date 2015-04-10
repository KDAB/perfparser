TEMPLATE = lib
include(../backends.pri)
TARGET = ../../../../elfutils/ebl_i386

LIBS += ../../libcpu/libi386.a

SOURCES += \
    ../i386_auxv.c \
    ../i386_cfi.c \
    ../i386_corenote.c \
    ../i386_init.c \
    ../i386_initreg.c \
    ../i386_regs.c \
    ../i386_retval.c \
    ../i386_symbol.c \
    ../i386_syscall.c

HEADERS += \
    ../i386_reloc.def
