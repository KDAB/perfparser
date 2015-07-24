include(../elfutils.pri)
include(../../../paths.pri)

DESTDIR = $$PERFPARSER_ELFUTILS_BACKENDS_DESTDIR

TEMPLATE = lib
CONFIG += shared dll

target.path = $$PERFPARSER_ELFUTILS_BACKENDS_INSTALLDIR
INSTALLS += target

include(../libebl/eblheaders.pri)

HEADERS += \
    $$PWD/libebl_CPU.h \
    $$PWD/linux-core-note.c \
    $$PWD/x86_corenote.c \
    $$PWD/common-reloc.c
