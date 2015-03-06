TEMPLATE = lib
CONFIG += staticlib
TARGET = ../i386

include(../../elfutils.pri)
include(../../libasm/asmheaders.pri)
include(../../libebl/eblheaders.pri)
include(../cpuheaders.pri)

gendis.target = i386_dis.h
gendis.commands = make -f $$PWD/../extras.mk gendis=$$OUT_PWD/../i386_gendis/ srcdir=$$PWD/../ \
    i386_dis.h

OTHER_FILES += \
    $$PWD/../extras.mk

SOURCES += \
    $$PWD/../i386_disasm.c

INCLUDEPATH += \
    $$OUT_PWD/../i386_gendis

GENERATED_HEADERS += \
    $$OUT_PWD/i386_dis.h \
    $$OUT_PWD/../i386_gendis/i386.mnemonics

QMAKE_EXTRA_TARGETS += \
    gendis

PRE_TARGETDEPS += \
    i386_dis.h
