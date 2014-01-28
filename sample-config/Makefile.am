CONFIG_FILES = \
	naemon.cfg \
	resource.cfg \
	conf.d/commands.cfg \
	conf.d/printer.cfg \
	conf.d/switch.cfg \
	conf.d/timeperiods.cfg \
	conf.d/localhost.cfg \
	conf.d/contacts.cfg \
	conf.d/windows.cfg \
	conf.d/templates/services.cfg \
	conf.d/templates/hosts.cfg \
	conf.d/templates/contacts.cfg

EXTRA_DIST = $(CONFIG_FILES:.cfg=.cfg.in)

noinst_DATA = $(CONFIG_FILES)

CLEANFILES = $(CONFIG_FILES)

$(CONFIG_FILES) : % : %.in
	$(AM_V_GEN) sed \
		-e 's,@libdir[@],$(libdir),g' \
		-e 's,@libexecdir[@],$(libexecdir),g' \
		-e 's,@localstatedir[@],$(localstatedir),g' \
		-e 's,@lockfile[@],$(lockfile),g' \
		-e 's,@logdir[@],$(logdir),g' \
		-e 's,@naemon_grp[@],$(naemon_grp),g' \
		-e 's,@naemon_user[@],$(naemon_user),g' \
		-e 's,@sysconfdir[@],$(sysconfdir),g' \
		-e 's,@CHECKRESULTDIR[@],$(CHECKRESULTDIR),g' \
		-e 's,@TMPDIR[@],$(TMPDIR),g' \
		-e 's,@VERSION[@],$(VERSION),g' \
		$< > $@

# not install-share-hook, because sysconfdir is usually part of install-exec
install-exec-hook: $(CONFIG_FILES)
	if [ ! -d $(DESTDIR)$(sysconfdir) ]; then \
		$(install_sh_PROGRAM) -d $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 naemon.cfg    $(DESTDIR)$(sysconfdir)/naemon.cfg; \
		$(install_sh_PROGRAM) -m 644 resource.cfg  $(DESTDIR)$(sysconfdir)/resource.cfg; \
		$(install_sh_PROGRAM) -d $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 conf.d/commands.cfg            $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 conf.d/printer.cfg             $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 conf.d/switch.cfg              $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 conf.d/timeperiods.cfg         $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 conf.d/localhost.cfg           $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 conf.d/contacts.cfg            $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -m 644 conf.d/windows.cfg             $(DESTDIR)$(sysconfdir)/conf.d; \
		$(install_sh_PROGRAM) -d $(DESTDIR)$(sysconfdir)/conf.d/templates; \
		$(install_sh_PROGRAM) -m 644 conf.d/templates/services.cfg  $(DESTDIR)$(sysconfdir)/conf.d/templates; \
		$(install_sh_PROGRAM) -m 644 conf.d/templates/hosts.cfg     $(DESTDIR)$(sysconfdir)/conf.d/templates; \
		$(install_sh_PROGRAM) -m 644 conf.d/templates/contacts.cfg  $(DESTDIR)$(sysconfdir)/conf.d/templates; \
	fi
