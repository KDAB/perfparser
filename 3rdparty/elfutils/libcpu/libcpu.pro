TEMPLATE = subdirs

# We're not compiling the "bpf" disassembler as that requires specific linux headers which aren't
# available on most systems.
SUBDIRS = \
    i386_dis \
    i386_gendis \
    i386_mnemonics \
    i386 \
    x86_64_dis \
    x86_64_mnemonics \
    x86_64

i386_gendis.depends = i386_mnemonics
i386_dis.depends = i386_gendis i386_mnemonics
x86_64_dis.depends = i386_gendis x86_64_mnemonics
i386.depends = i386_dis
x86_64.depends = x86_64_dis
