TEMPLATE = aux
DESTDIR = $$OUT_PWD/..

gendis.target = i386_dis.h
gendis.commands = make -f $$PWD/../extras.mk mnemonics=$$OUT_PWD/../i386_mnemonics/ \
    gendis=$$OUT_PWD/../i386_gendis/ srcdir=$$PWD/../ i386_dis.h

QMAKE_EXTRA_TARGETS += gendis
PRE_TARGETDEPS += i386_dis.h

OTHER_FILES += $$PWD/../extras.mk
