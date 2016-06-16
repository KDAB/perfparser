TARGET = i386
DESTDIR = $$OUT_PWD/..

include(../../static.pri)
include(../../libasm/asmheaders.pri)
include(../../libebl/eblheaders.pri)
include(../cpuheaders.pri)

SOURCES += \
    $$PWD/../i386_disasm.c

INCLUDEPATH += \
    $$OUT_PWD/../i386_mnemonics \
    $$OUT_PWD/../i386_dis

GENERATED_HEADERS += \
    $$OUT_PWD/../i386_dis/i386_dis.h \
    $$OUT_PWD/../i386_mnemonics/i386.mnemonics
