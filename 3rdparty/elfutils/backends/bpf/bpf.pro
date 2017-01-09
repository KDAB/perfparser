TARGET = ebl_bpf
include(../backends.pri)

SOURCES += \
    ../bpf_init.c \
    ../bpf_regs.c

HEADERS += \
    ../bpf_reloc.def
