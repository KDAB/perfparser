TEMPLATE = lib
CONFIG += staticlib
TARGET = ../../elf64

include(../../elfutils.pri)
include(../elfheaders.pri)

SOURCES += \
    $$PWD/../elf64_checksum.c \
    $$PWD/../elf64_fsize.c \
    $$PWD/../elf64_getehdr.c \
    $$PWD/../elf64_getphdr.c \
    $$PWD/../elf64_getshdr.c \
    $$PWD/../elf64_newehdr.c \
    $$PWD/../elf64_newphdr.c \
    $$PWD/../elf64_offscn.c \
    $$PWD/../elf64_updatefile.c \
    $$PWD/../elf64_updatenull.c \
    $$PWD/../elf64_xlatetof.c \
    $$PWD/../elf64_xlatetom.c \
