TEMPLATE = subdirs

# split out libelf's 32bit and 64bit specific parts as qmake will see the include directives in
# the 64bit versions and then not compile the 32bit ones

SUBDIRS = \
    backends \
    lib \
    libasm \
    libcpu \
    libebl \
    libelf \
    libelf/elf32 \
    libelf/elf64 \
    libdw \
    libdwelf \
    libdwfl

libcpu.depends = lib
backends.depends = libcpu

OTHER_FILES += \
    COPYING \
    COPYING-GPLV2 \
    COPYING-LGPLV3 \
    AUTHORS \
    THANKS

