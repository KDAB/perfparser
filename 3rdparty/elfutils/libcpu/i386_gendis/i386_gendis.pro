TEMPLATE = app

LIBS += ../../lib/libeu.a
QMAKE_CFLAGS += -Wno-unused-function

include(../../elfutils.pri)
include(../../lib/libheaders.pri)

mylex.target = i386_lex.c
mylex.depends = i386_parse.c
mylex.commands = flex -Pi386_ -o i386_lex.c $$PWD/../i386_lex.l

myyacc.target = i386_parse.c
myyacc.commands = bison -pi386_ -d -o i386_parse.c $$PWD/../i386_parse.y

OTHER_FILES += \
    $$PWD/../i386_lex.l \
    $$PWD/../i386_parse.y

SOURCES += \
    $$PWD/../i386_gendis.c

GENERATED_SOURCES += \
    i386_parse.c \
    i386_lex.c

GENERATED_HEADERS += \
    i386_parse.h

QMAKE_EXTRA_TARGETS += \
    mylex \
    myyacc

DEFINES += NMNES='$(shell wc -l < ../i386_mnemonics/i386.mnemonics)'
