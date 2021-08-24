QT += testlib
QT -= gui

CONFIG += testcase strict_flags warn_on

INCLUDEPATH += ../../../app

TARGET = tst_finddebugsym

include(../../../elfutils.pri)

SOURCES += \
    tst_finddebugsym.cpp \
    ../../../app/perfsymboltable.cpp

HEADERS += \
    ../../../app/perfsymboltable.h

OTHER_FILES += finddebugsym.qbs
