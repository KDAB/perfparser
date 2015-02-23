include(../libasm/asmheaders.pri)
include(../libdw/dwheaders.pri)
include(../libelf/elfheaders.pri)

HEADERS += \
    $$PWD/ebl-hooks.h \
    $$PWD/libebl.h \
    $$PWD/libeblP.h

INCLUDEPATH += $$PWD
