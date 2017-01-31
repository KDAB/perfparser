TARGET = ../ebl

include(../static.pri)
include(../libelf/elfheaders.pri)
include(../libasm/asmheaders.pri)
include(../libdw/dwheaders.pri)
include(eblheaders.pri)

SOURCES += \
    $$PWD/ebl_check_special_section.c \
    $$PWD/ebl_check_special_symbol.c \
    $$PWD/ebl_syscall_abi.c \
    $$PWD/eblabicfi.c \
    $$PWD/eblauxvinfo.c \
    $$PWD/eblbackendname.c \
    $$PWD/eblbsspltp.c \
    $$PWD/eblcheckobjattr.c \
    $$PWD/eblcheckreloctargettype.c \
    $$PWD/eblclosebackend.c \
    $$PWD/eblcopyrelocp.c \
    $$PWD/eblcorenote.c \
    $$PWD/eblcorenotetypename.c \
    $$PWD/ebldebugscnp.c \
    $$PWD/ebldwarftoregno.c \
    $$PWD/ebldynamictagcheck.c \
    $$PWD/ebldynamictagname.c \
    $$PWD/eblelfclass.c \
    $$PWD/eblelfdata.c \
    $$PWD/eblelfmachine.c \
    $$PWD/eblgotpcreloccheck.c \
    $$PWD/eblgstrtab.c \
    $$PWD/eblinitreg.c \
    $$PWD/eblmachineflagcheck.c \
    $$PWD/eblmachineflagname.c \
    $$PWD/eblmachinesectionflagcheck.c \
    $$PWD/eblnonerelocp.c \
    $$PWD/eblnormalizepc.c \
    $$PWD/eblobjnote.c \
    $$PWD/eblobjnotetypename.c \
    $$PWD/eblopenbackend.c \
    $$PWD/eblosabiname.c \
    $$PWD/eblreginfo.c \
    $$PWD/eblrelativerelocp.c \
    $$PWD/eblrelocsimpletype.c \
    $$PWD/eblreloctypecheck.c \
    $$PWD/eblreloctypename.c \
    $$PWD/eblrelocvaliduse.c \
    $$PWD/eblresolvesym.c \
    $$PWD/eblretval.c \
    $$PWD/eblsectionname.c \
    $$PWD/eblsectionstripp.c \
    $$PWD/eblsectiontypename.c \
    $$PWD/eblsegmenttypename.c \
    $$PWD/eblstother.c \
    $$PWD/eblstrtab.c \
    $$PWD/eblsymbolbindingname.c \
    $$PWD/eblsymboltypename.c \
    $$PWD/eblsysvhashentrysize.c \
    $$PWD/eblunwind.c \
    $$PWD/eblwstrtab.c
