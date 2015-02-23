TEMPLATE = lib
CONFIG += staticlib
TARGET = ../../elf32

include(../../elfutils.pri)
include(../elfheaders.pri)

SOURCES += \
    $$PWD/../elf32_checksum.c \
    $$PWD/../elf32_fsize.c \
    $$PWD/../elf32_getehdr.c \
    $$PWD/../elf32_getphdr.c \
    $$PWD/../elf32_getshdr.c \
    $$PWD/../elf32_newehdr.c \
    $$PWD/../elf32_newphdr.c \
    $$PWD/../elf32_offscn.c \
    $$PWD/../elf32_updatefile.c \
    $$PWD/../elf32_updatenull.c \
    $$PWD/../elf32_xlatetof.c \
    $$PWD/../elf32_xlatetom.c \

