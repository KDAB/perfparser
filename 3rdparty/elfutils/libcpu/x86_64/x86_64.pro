TARGET = ../x86_64

include(../../static.pri)
include(../../libasm/asmheaders.pri)
include(../../libebl/eblheaders.pri)
include(../cpuheaders.pri)

gendis.target = x86_64_dis.h
gendis.commands = make -f $$PWD/../extras.mk gendis=$$OUT_PWD/../i386_gendis/ srcdir=$$PWD/../ \
    x86_64_dis.h

OTHER_FILES += \
    $$PWD/../extras.mk

SOURCES += \
    $$PWD/../x86_64_disasm.c

INCLUDEPATH += \
    $$OUT_PWD/../i386_gendis \
    $$OUT_PWD

GENERATED_HEADERS += \
    $$OUT_PWD/x86_64_dis.h \
    $$OUT_PWD/../i386_gendis/x86_64.mnemonics

QMAKE_EXTRA_TARGETS += \
    gendis

PRE_TARGETDEPS += \
    x86_64_dis.h
