#-------------------------------------------------
#
# Project created by QtCreator 2014-08-14T10:44:20
#
#-------------------------------------------------

QT       += core network

QT       -= gui

QMAKE_LFLAGS += -Wl,-rpath,\'\$\$ORIGIN/elfutils/backends\'
LIBS += -Wl,--start-group \
        ../3rdparty/elfutils/libdw.a \
        ../3rdparty/elfutils/libdwfl.a \
        ../3rdparty/elfutils/libelf.a \
        ../3rdparty/elfutils/libelf32.a \
        ../3rdparty/elfutils/libelf64.a \
        ../3rdparty/elfutils/libebl.a \
        ../3rdparty/elfutils/libdwelf.a \
        -Wl,--end-group \
        -lz -ldl -liberty

TARGET = ../perfparser
CONFIG   += console c++11
CONFIG   -= app_bundle

TEMPLATE = app

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

include(../3rdparty/elfutils/elfutils.pri)
include(../3rdparty/elfutils/libdwfl/dwflheaders.pri)
include(../3rdparty/elfutils/libebl/eblheaders.pri)
include(../3rdparty/elfutils/libdwelf/dwelfheaders.pri)
