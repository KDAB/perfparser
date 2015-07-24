TARGET = eu

include(../static.pri)

SOURCES += \
    $$PWD/color.c \
    $$PWD/crc32_file.c \
    $$PWD/crc32.c \
    $$PWD/md5.c \
    $$PWD/next_prime.c \
    $$PWD/sha1.c \
    $$PWD/xmalloc.c \
    $$PWD/xstrdup.c \
    $$PWD/xstrndup.c

