TARGET = ../dwfl

include(../static.pri)
include(dwflheaders.pri)
include(../libdw/dwheaders.pri)
include(../libebl/eblheaders.pri)
include(../libdwelf/dwelfheaders.pri)

# We're not using lzma.c or bzip2.c as that would give us additional dependencies at for little
# benefit
SOURCES += \
    $$PWD/argp-std.c \
    $$PWD/core-file.c \
    $$PWD/cu.c \
    $$PWD/derelocate.c \
    $$PWD/dwfl_addrdie.c \
    $$PWD/dwfl_addrdwarf.c \
    $$PWD/dwfl_addrmodule.c \
    $$PWD/dwfl_begin.c \
    $$PWD/dwfl_build_id_find_debuginfo.c \
    $$PWD/dwfl_build_id_find_elf.c \
    $$PWD/dwfl_cumodule.c \
    $$PWD/dwfl_dwarf_line.c \
    $$PWD/dwfl_end.c \
    $$PWD/dwfl_error.c \
    $$PWD/dwfl_frame_pc.c \
    $$PWD/dwfl_frame_regs.c \
    $$PWD/dwfl_frame.c \
    $$PWD/dwfl_getdwarf.c \
    $$PWD/dwfl_getmodules.c \
    $$PWD/dwfl_getsrc.c \
    $$PWD/dwfl_getsrclines.c \
    $$PWD/dwfl_line_comp_dir.c \
    $$PWD/dwfl_linecu.c \
    $$PWD/dwfl_lineinfo.c \
    $$PWD/dwfl_linemodule.c \
    $$PWD/dwfl_module_addrdie.c \
    $$PWD/dwfl_module_addrname.c \
    $$PWD/dwfl_module_addrsym.c \
    $$PWD/dwfl_module_build_id.c \
    $$PWD/dwfl_module_dwarf_cfi.c \
    $$PWD/dwfl_module_eh_cfi.c \
    $$PWD/dwfl_module_getdwarf.c \
    $$PWD/dwfl_module_getelf.c \
    $$PWD/dwfl_module_getsrc_file.c \
    $$PWD/dwfl_module_getsrc.c \
    $$PWD/dwfl_module_getsym.c \
    $$PWD/dwfl_module_info.c \
    $$PWD/dwfl_module_nextcu.c \
    $$PWD/dwfl_module_register_names.c \
    $$PWD/dwfl_module_report_build_id.c \
    $$PWD/dwfl_module_return_value_location.c \
    $$PWD/dwfl_module.c \
    $$PWD/dwfl_nextcu.c \
    $$PWD/dwfl_onesrcline.c \
    $$PWD/dwfl_report_elf.c \
    $$PWD/dwfl_segment_report_module.c \
    $$PWD/dwfl_validate_address.c \
    $$PWD/dwfl_version.c \
    $$PWD/elf-from-memory.c \
    $$PWD/find-debuginfo.c \
    $$PWD/frame_unwind.c \
    $$PWD/gzip.c \
    $$PWD/image-header.c \
    $$PWD/libdwfl_crc32_file.c \
    $$PWD/libdwfl_crc32.c \
    $$PWD/lines.c \
    $$PWD/link_map.c \
    $$PWD/linux-core-attach.c \
    $$PWD/linux-kernel-modules.c \
    $$PWD/linux-pid-attach.c \
    $$PWD/linux-proc-maps.c \
    $$PWD/offline.c \
    $$PWD/open.c \
    $$PWD/relocate.c \
    $$PWD/segment.c
