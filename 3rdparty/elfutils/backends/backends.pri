include(../elfutils.pri)
include(../libebl/eblheaders.pri)

CONFIG -= qt

HEADERS += \
    $$PWD/libebl_CPU.h \
    $$PWD/linux-core-note.c \
    $$PWD/x86_corenote.c \
    $$PWD/common-reloc.c

INCLUDEPATH += $$PWD
