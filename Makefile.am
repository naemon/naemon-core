SUBDIRS = naemon tests sample-config t
AM_CPPFLAGS += -I$(top_builddir)

EXTRA_DIST = contrib

test: check

distclean-local:
	rm -rf configure Makefile.in aclocal.m4 autom4te.cache config.* *.gz compile depcomp missing

install-exec-hook:
	$(install_sh_PROGRAM) -d $(DESTDIR)$(CHECKRESULTDIR)
	$(install_sh_PROGRAM) -d $(DESTDIR)$(logdir)
	$(install_sh_PROGRAM) -d $(DESTDIR)$(logdir)/archives
	$(install_sh_PROGRAM) -d $(DESTDIR)$(libdir)
	$(install_sh_PROGRAM) -m 644 naemon.logrotate $(DESTDIR)$(logrotatedir)/naemon
	$(install_sh_PROGRAM) -m 644 daemon-init    $(DESTDIR)$(initrddir)/naemon

dox:
	rm -rf Documentation
	doxygen doxy.conf
