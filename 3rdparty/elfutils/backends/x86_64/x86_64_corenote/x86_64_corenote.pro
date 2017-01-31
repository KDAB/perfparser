TARGET = x86_64_corenote

include(../../../libasm/asmheaders.pri)
include(../../../libelf/elfheaders.pri)
include(../../../libebl/eblheaders.pri)
include(../../../libdw/dwheaders.pri)
include(../../../static.pri)

SOURCES +=  ../../x86_64_corenote.c
HEADERS += ../../x86_corenote.c

