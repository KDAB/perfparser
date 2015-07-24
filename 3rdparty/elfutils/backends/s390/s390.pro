TARGET = ebl_s390
include(../backends.pri)

SOURCES += \
    ../s390_cfi.c \
    ../s390_corenote.c \
    ../s390_init.c \
    ../s390_initreg.c \
    ../s390_regs.c \
    ../s390_retval.c \
    ../s390_symbol.c \
    ../s390_unwind.c \
    ../s390x_corenote.c

HEADERS += \
    ../s390_reloc.def
