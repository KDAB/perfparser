TEMPLATE = subdirs

SUBDIRS = \
    aarch64 \
    alpha \
    arm \
    i386 \
    ia64 \
    ppc \
    ppc64 \
    s390 \
    sh \
    sparc \
    tilegx \
    x86_64 \
    x86_64/x86_64_corenote \
    x86_64/x32_corenote

x86_64.depends = x86_64/x86_64_corenote x86_64/x32_corenote
