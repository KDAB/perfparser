TEMPLATE = subdirs

SUBDIRS = \
    i386_gendis \
    i386 \
    x86_64

i386.depends = i386_gendis
x86_64.depends = i386_gendis
