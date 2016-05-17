TEMPLATE = aux
DESTDIR = $$OUT_PWD/..

gendis.target = x86_64_dis.h
gendis.commands = make -f $$PWD/../extras.mk mnemonics=$$OUT_PWD/../x86_64_mnemonics/ \
    gendis=$$OUT_PWD/../i386_gendis/ srcdir=$$PWD/../ x86_64_dis.h

QMAKE_EXTRA_TARGETS += gendis
PRE_TARGETDEPS += x86_64_dis.h

OTHER_FILES += $$PWD/../extras.mk
