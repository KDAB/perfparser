TEMPLATE = subdirs
SUBDIRS = app

include (paths.pri)

defineReplace(elfutilsLibraryName) {
    RET = $$1
    linux: RET = lib$${RET}.so.$$2
    macos: RET = lib$${RET}.dylib
    win32: RET = $${RET}.dll
    return($$RET)
}

!isEmpty(BUILD_TESTS): SUBDIRS += tests

!isEmpty(PERFPARSER_BUNDLED_ELFUTILS) {
    SUBDIRS += 3rdparty/elfutils
    app.depends = 3rdparty/elfutils
    !isEmpty(BUILD_TESTS): tests.depends = 3rdparty/elfutils
} else:!isEmpty(PERFPARSER_ELFUTILS_INSTALLDIR) {
    unix:isEmpty(ELFUTILS_INSTALL_DIR): ELFUTILS_INSTALL_DIR = /usr

    inst_elfutils.files = \
        $$ELFUTILS_INSTALL_DIR/lib/$$elfutilsLibraryName(elf, 1) \
        $$ELFUTILS_INSTALL_DIR/lib/$$elfutilsLibraryName(dw, 1)

    inst_elfutils.path = $$PERFPARSER_ELFUTILS_INSTALLDIR
    inst_elfutils.CONFIG += no_check_exist no_default_install

    # only deploy the non-versioned backends. We are never dlopen'ing the versioned ones anyway.
    inst_backends.files = $$files($$ELFUTILS_INSTALL_DIR/lib/elfutils/*ebl_*.*)
    inst_backends.files -= $$files($$ELFUTILS_INSTALL_DIR/lib/elfutils/*ebl_*-*.*.*)
    inst_backends.path = $$PERFPARSER_ELFUTILS_BACKENDS_INSTALLDIR
    inst_backends.CONFIG += no_check_exist no_default_install

    INSTALLS += inst_backends inst_elfutils

    deploy.depends = install_inst_elfutils install_inst_backends

    linux {
        RPATH = $$relative_path($$PERFPARSER_ELFUTILS_BACKENDS_INSTALLDIR, \
                                $$PERFPARSER_ELFUTILS_INSTALLDIR)
        fix_dw_rpath.commands = chrpath -r \'\$\$ORIGIN/$$RPATH\' \
            $$PERFPARSER_ELFUTILS_INSTALLDIR/$$elfutilsLibraryName(dw, 1)
        fix_dw_rpath.depends = install_inst_elfutils
        deploy.depends += fix_dw_rpath
        QMAKE_EXTRA_TARGETS += fix_dw_rpath install_inst_elfutils
    }
}

QMAKE_EXTRA_TARGETS += deploy docs install_docs
