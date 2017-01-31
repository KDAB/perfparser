include(elfutils.pri)
include(../../paths.pri)

DESTDIR = $$PERFPARSER_ELFUTILS_DESTDIR

linux-* {
    INSTALL_RPATH = $$relative_path($$PERFPARSER_ELFUTILS_BACKENDS_INSTALLDIR,\
                                    $$PERFPARSER_ELFUTILS_INSTALLDIR)
    DEST_RPATH = $$relative_path($$PERFPARSER_ELFUTILS_BACKENDS_DESTDIR,\
                                 $$PERFPARSER_ELFUTILS_DESTDIR)

    QMAKE_LFLAGS += \'-Wl,-z,origin\' \
                    \'-Wl,-rpath,\$\$ORIGIN/$$INSTALL_RPATH\' \
                    \'-Wl,-rpath,\$\$ORIGIN/$$DEST_RPATH\' \
                    \'-Wl,-rpath,\$\$ORIGIN\'
}

TEMPLATE = lib
CONFIG += shared dll

target.path = $$PERFPARSER_ELFUTILS_INSTALLDIR
INSTALLS += target
