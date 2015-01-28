#-------------------------------------------------
#
# Project created by QtCreator 2014-08-14T10:44:20
#
#-------------------------------------------------

QT       += core network

QT       -= gui

LIBS += -ldw -lbfd

TARGET = perfparser
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
