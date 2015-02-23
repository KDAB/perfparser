TEMPLATE = subdirs

SUBDIRS = app 3rdparty/elfutils

app.depends = 3rdparty/elfutils
