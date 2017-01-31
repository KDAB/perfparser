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
    s390/s390_corenote \
    s390/s390x_corenote \
    sh \
    sparc \
    sparc/sparc_corenote \
    sparc/sparc64_corenote \
    tilegx \
    x86_64 \
    x86_64/x86_64_corenote \
    x86_64/x32_corenote

x86_64.depends = x86_64/x86_64_corenote x86_64/x32_corenote
s390.depends = s390/s390_corenote s390/s390x_corenote
sparc.depends = sparc/sparc_corenote sparc/sparc64_corenote
