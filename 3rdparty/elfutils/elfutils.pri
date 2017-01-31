VERSION = 0.168
QMAKE_CFLAGS += -std=gnu99
DEFINES += HAVE_CONFIG_H _GNU_SOURCE

HEADERS += \
    $$PWD/version.h \
    $$PWD/config.h

CONFIG -= qt

INCLUDEPATH += $$PWD

include(lib/libheaders.pri)
