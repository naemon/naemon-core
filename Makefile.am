SUBDIRS = naemon tests sample-config t t-tap tap
AM_CPPFLAGS += -I$(top_builddir)

EXTRA_DIST = contrib

all-local: manpages

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

manpages: naemon naemonstats shadownaemon oconfsplit
	help2man --no-info --section=8 --version-string="naemon $(PACKAGE_VERSION)" \
		--help-option=-h -n "monitoring core" \
		naemon > naemon.8
	help2man --no-info --section=8 --version-string="naemonstats $(PACKAGE_VERSION)" \
		--help-option=-h -n "gather statistics from naemon core" \
		naemonstats > naemonstats.8
	help2man --no-info --section=8 --version-string="shadownaemon $(PACKAGE_VERSION)" \
		--help-option=-h -n "shadow remote cores via livestatus" \
		shadownaemon > shadownaemon.8
	help2man --no-info --section=8 --version-string="oconfsplit $(PACKAGE_VERSION)" \
		--help-option=-h -n "split naemon configuration by hostgroups" \
		oconfsplit > oconfsplit.8

install-man-hook:
	mkdir -p $(mandir)/man8/
	install -m 644 naemon.8       $(DESTDIR)$(mandir)/man8/
	install -m 644 naemonstats.8  $(DESTDIR)$(mandir)/man8/
	install -m 644 shadownaemon.8 $(DESTDIR)$(mandir)/man8/
	install -m 644 oconfsplit.8   $(DESTDIR)$(mandir)/man8/
