#-------------------------------------------------
#
# Project created by QtCreator 2014-08-14T10:44:20
#
#-------------------------------------------------

QT     = core network
CONFIG += c++11 console
CONFIG -= app_bundle

include(../paths.pri)
RPATH = $$relative_path($$PERFPARSER_ELFUTILS_INSTALLDIR, $$PERFPARSER_APP_INSTALLDIR)

QMAKE_LFLAGS += -Wl,-z,origin \'-Wl,-rpath,\$\$ORIGIN/$$RPATH\'
LIBS += -L$$PERFPARSER_ELFUTILS_DESTDIR -ldw -lelf

DESTDIR = $$PERFPARSER_APP_DESTDIR
target.path = $$PERFPARSER_APP_INSTALLDIR
INSTALLS += target

TARGET = perfparser

SOURCES += main.cpp \
    perfattributes.cpp \
    perfheader.cpp \
    perffilesection.cpp \
    perffeatures.cpp \
    perfdata.cpp \
    perfunwind.cpp \
    perfregisterinfo.cpp \
    perfstdin.cpp

HEADERS += \
    perfattributes.h \
    perfheader.h \
    perffilesection.h \
    perffeatures.h \
    perfdata.h \
    perfunwind.h \
    perfregisterinfo.h \
    perfstdin.h

include(../3rdparty/elfutils/libdwfl/dwflheaders.pri)
include(../3rdparty/elfutils/libebl/eblheaders.pri)
include(../3rdparty/elfutils/libdwelf/dwelfheaders.pri)
