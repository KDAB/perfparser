#-------------------------------------------------
#
# Project created by QtCreator 2014-08-14T10:44:20
#
#-------------------------------------------------

QT     = core network
CONFIG += c++11 console
CONFIG -= app_bundle

include(../paths.pri)
include(../elfutils.pri)

DESTDIR = $$PERFPARSER_APP_DESTDIR
target.path = $$PERFPARSER_APP_INSTALLDIR
INSTALLS += target

TARGET = perfparser

SOURCES += main.cpp \
    perfaddresscache.cpp \
    perfattributes.cpp \
    perfheader.cpp \
    perffilesection.cpp \
    perffeatures.cpp \
    perfdata.cpp \
    perfunwind.cpp \
    perfregisterinfo.cpp \
    perfstdin.cpp \
    perfsymboltable.cpp \
    perfelfmap.cpp \
    perfkallsyms.cpp \
    perftracingdata.cpp

HEADERS += \
    perfaddresscache.h \
    perfattributes.h \
    perfheader.h \
    perffilesection.h \
    perffeatures.h \
    perfdata.h \
    perfunwind.h \
    perfregisterinfo.h \
    perfstdin.h \
    perfsymboltable.h \
    perfelfmap.h \
    perfkallsyms.h \
    perftracingdata.h

OTHER_FILES += app.qbs
