TEMPLATE = aux
DESTDIR = $$OUT_PWD/..

mnemonics.target = x86_64.mnemonics
mnemonics.commands = make -f $$PWD/../extras.mk srcdir=$$PWD/../ x86_64.mnemonics

OTHER_FILES = \
    $$PWD/../extras.mk \
    $$PWD/../defs/i386 \

QMAKE_EXTRA_TARGETS += mnemonics
PRE_TARGETDEPS += x86_64.mnemonics
