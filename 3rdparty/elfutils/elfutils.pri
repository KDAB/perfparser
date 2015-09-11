VERSION = 0.163
QMAKE_CFLAGS += -std=gnu99
DEFINES += HAVE_CONFIG_H _GNU_SOURCE

equals(QT_ARCH, i386)  {
    DEFINES += QMAKE_ARCH=32
}

equals(QT_ARCH, x86_64) {
    DEFINES += QMAKE_ARCH=64
}

HEADERS += \
    $$PWD/version.h \
    $$PWD/config.h

CONFIG -= qt

INCLUDEPATH += $$PWD

include(lib/libheaders.pri)
