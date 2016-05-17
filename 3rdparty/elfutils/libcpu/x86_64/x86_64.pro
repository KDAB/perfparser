TARGET = x86_64
DESTDIR = $$OUT_PWD/..

include(../../static.pri)
include(../../libasm/asmheaders.pri)
include(../../libebl/eblheaders.pri)
include(../cpuheaders.pri)

SOURCES += \
    $$PWD/../x86_64_disasm.c

INCLUDEPATH += \
    $$OUT_PWD/../x86_64_mnemonics \
    $$OUT_PWD/../x86_64_dis

GENERATED_HEADERS += \
    $$OUT_PWD/../x86_64_dis/x86_64_dis.h \
    $$OUT_PWD/../x86_64_mnemonics/x86_64.mnemonics
