TEMPLATE = subdirs
SUBDIRS = app

!isEmpty(PERFPARSER_BUNDLED_ELFUTILS) {
    SUBDIRS += 3rdparty/elfutils
    app.depends = 3rdparty/elfutils
}
