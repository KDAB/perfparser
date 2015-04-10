TEMPLATE = lib
include(../backends.pri)
TARGET = ../../../../elfutils/ebl_arm

SOURCES += \
    ../arm_attrs.c \
    ../arm_auxv.c \
    ../arm_cfi.c \
    ../arm_corenote.c \
    ../arm_init.c \
    ../arm_initreg.c \
    ../arm_regs.c \
    ../arm_retval.c \
    ../arm_symbol.c

HEADERS += \
    ../arm_reloc.def
