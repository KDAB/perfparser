
%_defs: $(srcdir)/defs/i386
	m4 -D$* -DDISASSEMBLER $< > $@T
	mv -f $@T $@

%.mnemonics: %_defs
	sed '1,/^%%/d;/^#/d;/^[[:space:]]*$$/d;s/[^:]*:\([^[:space:]]*\).*/MNE(\1)/;s/{[^}]*}//g;/INVALID/d' \
	  $< | sort -u > $@

%_dis.h: $(mnemonics)/%_defs $(gendis)/i386_gendis
	$(gendis)/i386_gendis $< > $@T
	mv -f $@T $@

.PRECIOUS: %_defs
