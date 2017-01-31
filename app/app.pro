#-------------------------------------------------
#
# Project created by QtCreator 2014-08-14T10:44:20
#
#-------------------------------------------------

QT     = core network
CONFIG += c++11 console
CONFIG -= app_bundle

include(../paths.pri)

!isEmpty(PERFPARSER_BUNDLED_ELFUTILS) {
    linux-* {
        RPATH = $$relative_path($$PERFPARSER_ELFUTILS_INSTALLDIR, $$PERFPARSER_APP_INSTALLDIR)
        QMAKE_LFLAGS += -Wl,-z,origin \'-Wl,-rpath,\$\$ORIGIN/$$RPATH\'
    }

    LIBS += -L$$PERFPARSER_ELFUTILS_DESTDIR

    include(../3rdparty/elfutils/libdwfl/dwflheaders.pri)
    include(../3rdparty/elfutils/libelf/elfheaders.pri)
    include(../3rdparty/elfutils/libdw/dwheaders.pri)
    include(../3rdparty/elfutils/libebl/eblheaders.pri)
    include(../3rdparty/elfutils/libdwelf/dwelfheaders.pri)
    include(../3rdparty/elfutils/lib/libheaders.pri)
} else {
    INCLUDEPATH += /usr/include/elfutils
}

# We require major version 0 from elfutils
LIBS += -l$$libraryRefName(dw, 0) -l$$libraryRefName(elf, 0)

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
    perfstdin.cpp \
    perfsymboltable.cpp \
    perfelfmap.cpp \
    perfkallsyms.cpp

HEADERS += \
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
    perfkallsyms.h
