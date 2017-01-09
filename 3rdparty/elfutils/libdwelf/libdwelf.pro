TARGET = ../dwelf

include(../static.pri)
include(dwelfheaders.pri)
include(../libdw/dwheaders.pri)
include(../libdwfl/dwflheaders.pri)
include(../libebl/eblheaders.pri)

SOURCES += \
    $$PWD/dwelf_dwarf_gnu_debugaltlink.c \
    $$PWD/dwelf_elf_gnu_build_id.c \
    $$PWD/dwelf_elf_gnu_debuglink.c \
    $$PWD/dwelf_scn_gnu_compressed_size.c \
    $$PWD/dwelf_strtab.c
