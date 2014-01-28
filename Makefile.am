SUBDIRS = naemon tests sample-config
AM_CPPFLAGS += -I$(top_builddir)

EXTRA_DIST = naemon.spec contrib

test: check

distclean-local:
	rm -rf configure Makefile.in aclocal.m4 autom4te.cache config.* *.gz compile depcomp missing

install-rc:
	$(install_sh_PROGRAM) -m 644 daemon-init    $(DESTDIR)$(initrddir)/naemon

install-logrotate:
	$(install_sh_PROGRAM) -m 644 naemon.logrotate $(DESTDIR)$(logrotatedir)/naemon
	mkdir -p $(DESTDIR)$(logdir)/archives

install-data-local:
	mkdir -p $(DESTDIR)/var/cache/naemon/checkresults
	mkdir -p $(DESTDIR)/var/lib/naemon
	mkdir -p $(DESTDIR)/var/log/naemon

dox:
	rm -rf Documentation
	doxygen doxy.conf
