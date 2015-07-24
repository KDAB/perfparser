TARGET = ebl_x86_64
include(../backends.pri)

LIBS += ../../libcpu/libx86_64.a

SOURCES += \
    ../x86_64_cfi.c \
    ../x86_64_corenote.c \
    ../x86_64_init.c \
    ../x86_64_initreg.c \
    ../x86_64_regs.c \
    ../x86_64_retval.c \
    ../x86_64_symbol.c \
    ../x86_64_syscall.c \
    ../x86_corenote.c \
    ../i386_auxv.c # x86_64_auxv_info is an alias for i386_auxv_info

HEADERS += \
    ../x86_64_reloc.def
