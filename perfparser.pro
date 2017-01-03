TEMPLATE = subdirs
SUBDIRS = app

!isEmpty(BUILD_TESTS): SUBDIRS += tests

!isEmpty(PERFPARSER_BUNDLED_ELFUTILS) {
    SUBDIRS += 3rdparty/elfutils
    app.depends = 3rdparty/elfutils
    !isEmpty(BUILD_TESTS): tests.depends = 3rdparty/elfutils
}
