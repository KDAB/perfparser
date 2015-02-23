VERSION = 0.161
QMAKE_CFLAGS += -std=gnu99
DEFINES += HAVE_CONFIG_H _GNU_SOURCE

HEADERS += \
    $$PWD/version.h \
    $$PWD/config.h

INCLUDEPATH += $$PWD
include(lib/libheaders.pri)
