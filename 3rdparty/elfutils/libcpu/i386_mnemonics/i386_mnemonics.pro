TEMPLATE = aux
DESTDIR = $$OUT_PWD

include(../tools.pri)

mnemonics.target = i386.mnemonics
mnemonics.commands = $$MAKE -f $$PWD/../extras.mk srcdir=$$PWD/../ i386.mnemonics

OTHER_FILES = \
    $$PWD/../extras.mk \
    $$PWD/../defs/i386 \

QMAKE_EXTRA_TARGETS += mnemonics
PRE_TARGETDEPS += i386.mnemonics
