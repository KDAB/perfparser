TARGET = ebl_sparc

# we need these before libeu, as they use symbols from libeu
LIBS += sparc_corenote/libsparc_corenote.a sparc64_corenote/libsparc64_corenote.a

include(../backends.pri)

SOURCES += \
    ../sparc_auxv.c \
    ../sparc_attrs.c \
    ../sparc_cfi.c \
    ../sparc_init.c \
    ../sparc_initreg.c \
    ../sparc_regs.c \
    ../sparc_retval.c \
    ../sparc_symbol.c

HEADERS += \
    ../sparc_reloc.def
