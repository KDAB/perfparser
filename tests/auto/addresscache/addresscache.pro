QT += testlib
QT -= gui

CONFIG += testcase strict_flags warn_on

INCLUDEPATH += ../../../app

TARGET = tst_addresscache

include(../../../elfutils.pri)

SOURCES += \
    tst_addresscache.cpp \
    ../../../app/perfelfmap.cpp \
    ../../../app/perfaddresscache.cpp \
    ../../../app/perfdwarfdiecache.cpp

HEADERS += \
    ../../../app/perfelfmap.h \
    ../../../app/perfaddresscache.h \
    ../../../app/perfdwarfdiecache.h

OTHER_FILES += addresscache.qbs
