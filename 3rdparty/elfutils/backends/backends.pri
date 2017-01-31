include(../elfutils.pri)
include(../../../paths.pri)

TARGET = $$TARGET$$qtPlatformTargetSuffix()
DESTDIR = $$PERFPARSER_ELFUTILS_BACKENDS_DESTDIR

TEMPLATE = lib
CONFIG += shared dll
LIBS += ../../lib/libeu.a -L$$PERFPARSER_ELFUTILS_DESTDIR -l$$libraryName(elf) -l$$libraryName(dw)

target.path = $$PERFPARSER_ELFUTILS_BACKENDS_INSTALLDIR
INSTALLS += target

include(../libasm/asmheaders.pri)
include(../libebl/eblheaders.pri)
include(../libelf/elfheaders.pri)
include(../libdw/dwheaders.pri)

HEADERS += \
    $$PWD/libebl_CPU.h \
    $$PWD/linux-core-note.c \
    $$PWD/x86_corenote.c \
    $$PWD/common-reloc.c
