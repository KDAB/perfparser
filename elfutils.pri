!isEmpty(ELFUTILS_INSTALL_DIR) {
    INCLUDEPATH += $$ELFUTILS_INSTALL_DIR/include $$ELFUTILS_INSTALL_DIR/include/elfutils
    LIBS += -L$$ELFUTILS_INSTALL_DIR/lib
} else:unix {
    INCLUDEPATH += /usr/include/elfutils
}

LIBS += -ldw -lelf

win32 {
    LIBS += -leu_compat
}

linux:!isEmpty(PERFPARSER_ELFUTILS_INSTALLDIR) {
    RPATH = $$relative_path($$PERFPARSER_ELFUTILS_INSTALLDIR, $$PERFPARSER_APP_INSTALLDIR)
    QMAKE_LFLAGS += -Wl,-z,origin \'-Wl,-rpath,\$\$ORIGIN/$$RPATH\'
}
