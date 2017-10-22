QT += testlib
QT -= gui

CONFIG += testcase strict_flags warn_on

INCLUDEPATH += ../../../app

TARGET = tst_addresscache

SOURCES += \
    tst_addresscache.cpp \
    ../../../app/perfelfmap.cpp \
    ../../../app/perfaddresscache.cpp

HEADERS += \
    ../../../app/perfelfmap.h \
    ../../../app/perfaddresscache.h

OTHER_FILES += addresscache.qbs
