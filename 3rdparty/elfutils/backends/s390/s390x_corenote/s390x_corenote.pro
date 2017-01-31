TARGET = s390x_corenote

include(../../../libasm/asmheaders.pri)
include(../../../libebl/eblheaders.pri)
include(../../../libelf/elfheaders.pri)
include(../../../libdw/dwheaders.pri)
include(../../../static.pri)

SOURCES += ../../s390x_corenote.c
HEADERS += ../../s390_corenote.c
