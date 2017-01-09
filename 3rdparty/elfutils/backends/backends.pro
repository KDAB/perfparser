TEMPLATE = subdirs

SUBDIRS = \
    aarch64 \
    alpha \
    arm \
    bpf \
    i386 \
    ia64 \
    m68k \
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
