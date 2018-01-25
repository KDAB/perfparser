QT += testlib
QT -= gui

CONFIG += testcase strict_flags warn_on

INCLUDEPATH += ../../../app

TARGET = tst_perfstdin

SOURCES += \
    tst_perfstdin.cpp \
    ../../../app/perfstdin.cpp

HEADERS += \
    ../../../app/perfstdin.h

OTHER_FILES += perfstdin.qbs
