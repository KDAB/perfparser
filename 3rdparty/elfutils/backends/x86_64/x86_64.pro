TARGET = ebl_x86_64

# we need these before libeu, as they use symbols from libeu
LIBS += ../../libcpu/libx86_64.a libx86_64_corenote.a libx32_corenote.a

include(../backends.pri)

SOURCES += \
    ../x86_64_cfi.c \
    ../x86_64_init.c \
    ../x86_64_initreg.c \
    ../x86_64_regs.c \
    ../x86_64_retval.c \
    ../x86_64_symbol.c \
    ../x86_64_syscall.c \
    ../x86_64_unwind.c \
    ../i386_auxv.c # x86_64_auxv_info is an alias for i386_auxv_info

HEADERS += \
    ../x86_64_reloc.def
