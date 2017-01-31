TARGET = ebl_i386

# we need this before libeu, as it uses symbols from libeu
LIBS += ../../libcpu/libi386.a

include(../backends.pri)

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
    ../i386_reloc.def \
    ../x86_corenote.c
