TEMPLATE = subdirs

isEmpty(ELFUTILS_INSTALL_DIR) {
    unix {
        ELFUTILS_INCLUDE_DIR = /usr/include
    } else {
        warning("Cannot automatically infer the elfutils include and lib directories.")
    }
} else {
    ELFUTILS_INCLUDE_DIR = $$ELFUTILS_INSTALL_DIR/include
}

exists($$ELFUTILS_INCLUDE_DIR/libdwfl.h)|exists($$ELFUTILS_INCLUDE_DIR/elfutils/libdwfl.h) {
    SUBDIRS = app
    !isEmpty(BUILD_TESTS): SUBDIRS += tests

    include (paths.pri)

    defineReplace(elfutilsLibraryName) {
        RET = $$1
        linux: RET = lib$${RET}.so.$$2
        macos: RET = lib$${RET}.dylib
        win32: RET = $${RET}.dll
        return($$RET)
    }

    !isEmpty(PERFPARSER_ELFUTILS_INSTALLDIR) {
        ELFUTILS_LIB_DIR = $$ELFUTILS_INSTALL_DIR/lib
        inst_elfutils.files = \
            $$ELFUTILS_LIB_DIR/$$elfutilsLibraryName(elf, 1) \
            $$ELFUTILS_LIB_DIR/$$elfutilsLibraryName(dw, 1)

        win32: inst_elfutils.files += $$ELFUTILS_LIB_DIR/eu_compat.dll

        inst_elfutils.path = $$PERFPARSER_ELFUTILS_INSTALLDIR
        inst_elfutils.CONFIG += no_check_exist no_default_install

        # only deploy the non-versioned backends. We are never dlopen'ing the versioned ones anyway.
        inst_backends.files = $$files($$ELFUTILS_LIB_DIR/elfutils/*ebl_*.*)
        inst_backends.files -= $$files($$ELFUTILS_LIB_DIR/elfutils/*ebl_*-*.*.*)
        inst_backends.path = $$PERFPARSER_ELFUTILS_BACKENDS_INSTALLDIR
        inst_backends.CONFIG += no_check_exist no_default_install

        INSTALLS += inst_backends inst_elfutils

        deployqt.depends = install_inst_elfutils install_inst_backends

        linux {
            RPATH = $$relative_path($$PERFPARSER_ELFUTILS_BACKENDS_INSTALLDIR, \
                                    $$PERFPARSER_ELFUTILS_INSTALLDIR)
            fix_dw_rpath.commands = chrpath -r \'\$\$ORIGIN/$$RPATH\' \
                $$PERFPARSER_ELFUTILS_INSTALLDIR/$$elfutilsLibraryName(dw, 1)
            fix_dw_rpath.depends = install_inst_elfutils
            deployqt.depends += fix_dw_rpath
            QMAKE_EXTRA_TARGETS += fix_dw_rpath install_inst_elfutils
        }
    }
} else {
    warning("PerfParser is disabled. Set ELFUTILS_INSTALL_DIR to enable it.");
}

OTHER_FILES += perfparser.qbs

QMAKE_EXTRA_TARGETS += deployqt docs install_docs
