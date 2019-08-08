QT     = core
CONFIG += c++11 console
CONFIG -= app_bundle

CONFIG += strict_flags warn_on

INCLUDEPATH += ../../../app

include(../../../paths.pri)
include(../../auto/shared/shared.pri)

DEFINES += MANUAL_TEST
DESTDIR = $$PERFPARSER_APP_DESTDIR
target.path = $$PERFPARSER_APP_INSTALLDIR
INSTALLS += target

TARGET = perf2text

SOURCES += perf2text.cpp

OTHER_FILES += perf2text.qbs
