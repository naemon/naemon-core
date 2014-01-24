SUBDIRS = naemon tests
AM_CPPFLAGS += -I$(top_builddir)

EXTRA_DIST = naemon.spec contrib

test: check

distclean-local:
	rm -rf configure Makefile.in aclocal.m4 autom4te.cache config.* *.gz compile depcomp missing

install-rc:
	$(install_sh_PROGRAM) -m 644 daemon-init    $(DESTDIR)$(initrddir)/naemon

install-sample-config:
	$(install_sh_PROGRAM) -m 644 sample-config/naemon.cfg    $(DESTDIR)$(sysconfdir)/naemon.cfg
	$(install_sh_PROGRAM) -m 644 sample-config/resource.cfg  $(DESTDIR)$(sysconfdir)/resource.cfg
	$(install_sh_PROGRAM) -d $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/commands.cfg            $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/printer.cfg             $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/switch.cfg              $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/timeperiods.cfg         $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/localhost.cfg           $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/contacts.cfg            $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/windows.cfg             $(DESTDIR)$(sysconfdir)/conf.d
	$(install_sh_PROGRAM) -d $(DESTDIR)$(sysconfdir)/conf.d/templates
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/templates/services.cfg  $(DESTDIR)$(sysconfdir)/conf.d/templates
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/templates/hosts.cfg     $(DESTDIR)$(sysconfdir)/conf.d/templates
	$(install_sh_PROGRAM) -m 644 sample-config/conf.d/templates/contacts.cfg  $(DESTDIR)$(sysconfdir)/conf.d/templates

install-data-local:
	mkdir -p $(DESTDIR)/var/cache/naemon/checkresults
	mkdir -p $(DESTDIR)/var/lib/naemon
	mkdir -p $(DESTDIR)/var/log/naemon
