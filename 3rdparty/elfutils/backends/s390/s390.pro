TARGET = ebl_s390

# we need these before libeu, as they use symbols from libeu
LIBS += s390_corenote/libs390_corenote.a s390x_corenote/libs390x_corenote.a

include(../backends.pri)

SOURCES += \
    ../s390_cfi.c \
    ../s390_init.c \
    ../s390_initreg.c \
    ../s390_regs.c \
    ../s390_retval.c \
    ../s390_symbol.c \
    ../s390_unwind.c

HEADERS += \
    ../s390_reloc.def
