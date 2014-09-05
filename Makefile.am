SUBDIRS = naemon tests sample-config t t-tap tap
AM_CPPFLAGS += -I$(top_builddir)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = naemon.pc

EXTRA_DIST = contrib naemon-uninstalled.pc.in naemon.pc.in

CLEANFILES = naemon-uninstalled.pc naemon.pc

test: check

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(CHECKRESULTDIR)
	$(mkinstalldirs) $(DESTDIR)$(logdir)
	$(mkinstalldirs) $(DESTDIR)$(logdir)/archives
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	[ -z $(logrotatedir) ] || $(install_sh_DATA) naemon.logrotate $(DESTDIR)$(logrotatedir)/naemon
	[ -z $(initrddir) ]    || $(install_sh_DATA) daemon-init      $(DESTDIR)$(initrddir)/naemon

uninstall-hook:
	[ -z $(logrotatedir) ] || rm $(DESTDIR)$(logrotatedir)/naemon
	[ -z $(initrddir) ]    || rm $(DESTDIR)$(initrddir)/naemon

