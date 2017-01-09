TARGET = elf

include(../dynamic.pri)
include(elfheaders.pri)

LIBS += -Wl,--whole-archive libelf32.a libelf64.a -Wl,--no-whole-archive

SOURCES += \
    $$PWD/elf_begin.c \
    $$PWD/elf_clone.c \
    $$PWD/elf_cntl.c \
    $$PWD/elf_compress.c \
    $$PWD/elf_compress_gnu.c \
    $$PWD/elf_end.c \
    $$PWD/elf_error.c \
    $$PWD/elf_fill.c \
    $$PWD/elf_flagdata.c \
    $$PWD/elf_flagehdr.c \
    $$PWD/elf_flagelf.c \
    $$PWD/elf_flagphdr.c \
    $$PWD/elf_flagscn.c \
    $$PWD/elf_flagshdr.c \
    $$PWD/elf_getarhdr.c \
    $$PWD/elf_getaroff.c \
    $$PWD/elf_getarsym.c \
    $$PWD/elf_getbase.c \
    $$PWD/elf_getdata_rawchunk.c \
    $$PWD/elf_getdata.c \
    $$PWD/elf_getident.c \
    $$PWD/elf_getphdrnum.c \
    $$PWD/elf_getscn.c \
    $$PWD/elf_getshdrnum.c \
    $$PWD/elf_getshdrstrndx.c \
    $$PWD/elf_gnu_hash.c \
    $$PWD/elf_hash.c \
    $$PWD/elf_kind.c \
    $$PWD/elf_memory.c \
    $$PWD/elf_ndxscn.c \
    $$PWD/elf_newdata.c \
    $$PWD/elf_newscn.c \
    $$PWD/elf_next.c \
    $$PWD/elf_nextscn.c \
    $$PWD/elf_rand.c \
    $$PWD/elf_rawdata.c \
    $$PWD/elf_rawfile.c \
    $$PWD/elf_readall.c \
    $$PWD/elf_scnshndx.c \
    $$PWD/elf_strptr.c \
    $$PWD/elf_update.c \
    $$PWD/elf_version.c \
    $$PWD/gelf_checksum.c \
    $$PWD/gelf_fsize.c \
    $$PWD/gelf_getauxv.c \
    $$PWD/gelf_getchdr.c \
    $$PWD/gelf_getclass.c \
    $$PWD/gelf_getdyn.c \
    $$PWD/gelf_getehdr.c \
    $$PWD/gelf_getlib.c \
    $$PWD/gelf_getmove.c \
    $$PWD/gelf_getnote.c \
    $$PWD/gelf_getphdr.c \
    $$PWD/gelf_getrel.c \
    $$PWD/gelf_getrela.c \
    $$PWD/gelf_getshdr.c \
    $$PWD/gelf_getsym.c \
    $$PWD/gelf_getsyminfo.c \
    $$PWD/gelf_getsymshndx.c \
    $$PWD/gelf_getverdaux.c \
    $$PWD/gelf_getverdef.c \
    $$PWD/gelf_getvernaux.c \
    $$PWD/gelf_getverneed.c \
    $$PWD/gelf_getversym.c \
    $$PWD/gelf_newehdr.c \
    $$PWD/gelf_newphdr.c \
    $$PWD/gelf_offscn.c \
    $$PWD/gelf_update_auxv.c \
    $$PWD/gelf_update_dyn.c \
    $$PWD/gelf_update_ehdr.c \
    $$PWD/gelf_update_lib.c \
    $$PWD/gelf_update_move.c \
    $$PWD/gelf_update_phdr.c \
    $$PWD/gelf_update_rel.c \
    $$PWD/gelf_update_rela.c \
    $$PWD/gelf_update_shdr.c \
    $$PWD/gelf_update_sym.c \
    $$PWD/gelf_update_syminfo.c \
    $$PWD/gelf_update_symshndx.c \
    $$PWD/gelf_update_verdaux.c \
    $$PWD/gelf_update_verdef.c \
    $$PWD/gelf_update_vernaux.c \
    $$PWD/gelf_update_verneed.c \
    $$PWD/gelf_update_versym.c \
    $$PWD/gelf_xlate.c \
    $$PWD/gelf_xlatetof.c \
    $$PWD/gelf_xlatetom.c \
    $$PWD/libelf_crc32.c \
    $$PWD/libelf_next_prime.c \
    $$PWD/nlist.c
