SUBDIRS = naemon tests sample-config t
AM_CPPFLAGS += -I$(top_builddir)

EXTRA_DIST = contrib

test: check

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(CHECKRESULTDIR)
	$(mkinstalldirs) $(DESTDIR)$(logdir)
	$(mkinstalldirs) $(DESTDIR)$(logdir)/archives
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(install_sh_DATA) naemon.logrotate $(DESTDIR)$(logrotatedir)/naemon
	$(install_sh_DATA) daemon-init      $(DESTDIR)$(initrddir)/naemon

uninstall-hook:
	rm $(DESTDIR)$(logrotatedir)/naemon
	rm $(DESTDIR)$(initrddir)/naemon

dox:
	rm -rf Documentation
	doxygen doxy.conf
