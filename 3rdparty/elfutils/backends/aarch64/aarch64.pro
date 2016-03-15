TARGET = ebl_aarch64
include(../backends.pri)

SOURCES += \
    ../aarch64_cfi.c \
    ../aarch64_corenote.c \
    ../aarch64_init.c \
    ../aarch64_initreg.c \
    ../aarch64_regs.c \
    ../aarch64_retval.c \
    ../aarch64_symbol.c \
    ../aarch64_unwind.c

HEADERS += \
    ../aarch64_reloc.def
