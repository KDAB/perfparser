TEMPLATE = app

CONFIG -= qt
LIBS += ../../libeu.a
QMAKE_CFLAGS += -Wno-unused-function

include(../../elfutils.pri)
include(../../lib/libheaders.pri)

mnemonics32.target = i386.mnemonics
mnemonics32.commands = make -f $$PWD/../extras.mk srcdir=$$PWD/../ i386.mnemonics

mnemonics64.target = x86_64.mnemonics
mnemonics64.commands = make -f $$PWD/../extras.mk srcdir=$$PWD/../ x86_64.mnemonics

mylex.target = lexresults
mylex.commands = lex -Pi386_ -o i386_lex.c $$PWD/../i386_lex.l

myyacc.target = yaccresults
myyacc.commands = yacc -pi386_ -d -o i386_parse.c $$PWD/../i386_parse.y

OTHER_FILES += \
    $$PWD/../extras.mk \
    $$PWD/../defs/i386 \
    $$PWD/../i386_lex.l \
    $$PWD/../i386_parse.y

SOURCES += \
    $$PWD/../i386_gendis.c

GENERATED_SOURCES += \
    i386_parse.c \
    i386_lex.c

GENERATED_HEADERS += \
    i386_parse.h

PRE_TARGETDEPS += \
    lexresults \
    yaccresults \
    i386.mnemonics \
    x86_64.mnemonics

QMAKE_EXTRA_TARGETS += \
    mylex \
    myyacc \
    mnemonics32 \
    mnemonics64

DEFINES += NMNES='$(shell wc -l < i386.mnemonics)'
