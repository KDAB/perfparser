QT += testlib
QT -= gui

CONFIG += testcase strict_flags warn_on

INCLUDEPATH += ../../../app

TARGET = tst_elfmap

SOURCES += \
    tst_elfmap.cpp \
    ../../../app/perfelfmap.cpp

HEADERS += \
    ../../../app/perfelfmap.h

OTHER_FILES += elfmap.qbs
