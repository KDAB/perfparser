QT += testlib
QT -= gui

CONFIG += testcase strict_flags warn_on

INCLUDEPATH += ../../../app

TARGET = tst_kallsyms

SOURCES += \
    tst_kallsyms.cpp \
    ../../../app/perfkallsyms.cpp

HEADERS += \
    ../../../app/perfkallsyms.h

OTHER_FILES += kallsyms.qbs
