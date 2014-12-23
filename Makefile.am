SUBDIRS = tap
AM_CPPFLAGS += -I$(abs_srcdir)/src -DNAEMON_COMPILATION
BUILT_SOURCES = src/naemon/wpres-phash.h src/naemon/buildopts.h

CONFIG_FILES = \
	sample-config/naemon.cfg \
	sample-config/resource.cfg \
	sample-config/conf.d/commands.cfg \
	sample-config/conf.d/printer.cfg \
	sample-config/conf.d/switch.cfg \
	sample-config/conf.d/timeperiods.cfg \
	sample-config/conf.d/localhost.cfg \
	sample-config/conf.d/contacts.cfg \
	sample-config/conf.d/windows.cfg \
	sample-config/conf.d/templates/services.cfg \
	sample-config/conf.d/templates/hosts.cfg \
	sample-config/conf.d/templates/contacts.cfg


EXTRA_DIST = src/naemon/buildopts.h.in
EXTRA_DIST += contrib naemon-uninstalled.pc.in naemon.pc.in
EXTRA_DIST += src/naemon/wpres-phash.h
EXTRA_DIST += $(CONFIG_FILES:.cfg=.cfg.in)
noinst_DATA = $(CONFIG_FILES)

src/naemon/wpres-phash.h: src/naemon/wpres.gperf
	$(AM_V_GEN) $(GPERF) --switch=1 --struct-type \
		--hash-function-name=wpres_key_phash \
		--lookup-function-name=wpres_get_key \
		--language=ANSI-C \
	$< > $@

src/naemon/buildopts.h: src/naemon/buildopts.h.in
	sed -e 's,@@NAEMON_SYSCONFDIR@@,$(pkgconfdir),' \
	 -e 's,@@NAEMON_LOCALSTATEDIR@@,$(localstatedir)/cache/naemon,' \
	 -e 's,@@NAEMON_LOGDIR@@,$(logdir),' \
	 -e 's,@@NAEMON_LOCKFILE@@,$(lockfile),' \
	$< > $@


nobase_pkginclude_HEADERS = \
	lib/bitmap.h    lib/kvvec.h       lib/kvvec_ekvstr.h lib/nsock.h    \
	lib/dkhash.h    lib/libnaemon.h   lib/nspath.h   lib/snprintf.h \
	lib/fanout.h    lib/libnagios.h   lib/nsutils.h  lib/squeue.h \
	lib/iobroker.h  lib/lnae-utils.h  lib/pqueue.h   lib/t-utils.h \
	lib/iocache.h   lib/lnag-utils.h  lib/runcmd.h   lib/worker.h \
	lib/rbtree.h

pkginclude_HEADERS = \
	src/naemon/broker.h			src/naemon/events.h			src/naemon/nagios.h			src/naemon/objects.h \
	src/naemon/workers.h		src/naemon/checks.h			src/naemon/flapping.h		src/naemon/nebcallbacks.h \
	src/naemon/checks_host.h	src/naemon/checks_service.h \
	src/naemon/perfdata.h		src/naemon/commands.h		src/naemon/globals.h		src/naemon/neberrors.h \
	src/naemon/query-handler.h  src/naemon/comments.h		src/naemon/nebmods.h \
	src/naemon/sehandlers.h		src/naemon/common.h         src/naemon/logging.h		src/naemon/nebmodules.h \
	src/naemon/shared.h			src/naemon/configuration.h  src/naemon/macros.h			src/naemon/nebstructs.h \
	src/naemon/sretention.h		src/naemon/defaults.h       src/naemon/naemon.h			src/naemon/nerd.h \
	src/naemon/statusdata.h		src/naemon/downtime.h       src/naemonstats/naemonstats.h	src/naemon/notifications.h \
	src/naemon/utils.h			src/naemon/buildopts.h      src/naemon/nm_alloc.h

common_sources = \
	src/naemon/broker.c src/naemon/broker.h \
	src/naemon/checks.c src/naemon/checks.h \
	src/naemon/checks_host.c src/naemon/checks_host.h \
	src/naemon/checks_service.c src/naemon/checks_service.h \
	src/naemon/commands.c src/naemon/commands.h \
	src/naemon/comments.c src/naemon/comments.h \
	src/naemon/common.h \
	src/naemon/configuration.c src/naemon/configuration.h \
	src/naemon/defaults.c src/naemon/defaults.h \
	src/naemon/downtime.c src/naemon/downtime.h \
	src/naemon/events.c src/naemon/events.h \
	src/naemon/flapping.c src/naemon/flapping.h \
	src/naemon/globals.h \
	src/naemon/logging.c src/naemon/logging.h \
	src/naemon/macros.c src/naemon/macros.h \
	src/naemon/nebcallbacks.h src/naemon/neberrors.h \
	src/naemon/nebmods.c src/naemon/nebmods.h \
	src/naemon/nebmodules.h src/naemon/nebstructs.h \
	src/naemon/nerd.c src/naemon/nerd.h \
	src/naemon/nm_alloc.c src/naemon/nm_alloc.h \
	src/naemon/notifications.c src/naemon/notifications.h \
	src/naemon/objects.c src/naemon/objects.h \
	src/naemon/perfdata.c src/naemon/perfdata.h \
	src/naemon/query-handler.c src/naemon/query-handler.h \
	src/naemon/sehandlers.c src/naemon/sehandlers.h \
	src/naemon/shared.c src/naemon/shared.h \
	src/naemon/sretention.c src/naemon/sretention.h \
	src/naemon/statusdata.c src/naemon/statusdata.h \
	src/naemon/utils.c src/naemon/utils.h \
	src/naemon/workers.c src/naemon/workers.h \
	src/naemon/xodtemplate.c src/naemon/xodtemplate.h \
	src/naemon/xpddefault.c src/naemon/xpddefault.h \
	src/naemon/xrddefault.c src/naemon/xrddefault.h \
	src/naemon/xsddefault.c src/naemon/xsddefault.h \
	src/naemon/nagios.h src/naemon/naemon.h \
	src/naemon/wpres.gperf \
	src/naemon/buildopts.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = naemon.pc

bin_PROGRAMS = src/naemon/naemon src/naemonstats/naemonstats src/shadownaemon/shadownaemon
manpages: $(bin_PROGRAMS)
	$(HELP2MAN) --no-info --section=8 --help-option=-h -n "monitoring core"                          ./src/naemon/naemon       > naemon.8
	$(HELP2MAN) --no-info --section=8 --help-option=-h -n "gather statistics from naemon core"       ./src/naemonstats/naemonstats  > naemonstats.8
	$(HELP2MAN) --no-info --section=8 --help-option=-h -n "shadow remote cores via livestatus"       ./src/shadownaemon/shadownaemon > shadownaemon.8

all-local: manpages

CLEANFILES = naemon-uninstalled.pc naemon.pc naemon.8 naemonstats.8 shadownaemon.8
CLEANFILES += src/naemon/wpres-phash.h src/naemon/buildopts.h
CLEANFILES += $(CONFIG_FILES)

src_naemon_naemon_SOURCES = src/naemon/naemon.c $(common_sources)
src_naemon_naemon_CPPFLAGS = $(AM_CPPFLAGS) -DPREFIX='"$(prefix)"'
src_naemon_naemon_LDADD = libnaemon.la -lm -ldl
src_naemon_naemon_LDFLAGS = -rdynamic

src_naemonstats_naemonstats_SOURCES = src/naemonstats/naemonstats.c src/naemon/buildopts.h src/naemon/defaults.h src/naemon/defaults.c src/naemon/common.h
src_naemonstats_naemonstats_LDADD = libnaemon.la

src_shadownaemon_shadownaemon_SOURCES = src/shadownaemon/shadownaemon.c src/shadownaemon/shadownaemon.h $(common_sources)
src_shadownaemon_shadownaemon_LDADD = libnaemon.la -lm -ldl
src_shadownaemon_shadownaemon_LDFLAGS = -rdynamic

LDADD = -lnaemon
lib_LTLIBRARIES = libnaemon.la

# details on library version numbers https://www.sourceware.org/autobook/autobook/autobook_91.html
libnaemon_la_LDFLAGS = -version-info 0:0:0
libnaemon_la_SOURCES = $(pkginclude_HEADERS) \
	lib/bitmap.c lib/dkhash.c lib/fanout.c lib/iobroker.c lib/iocache.c \
	lib/kvvec.c lib/kvvec_ekvstr.c lib/nsock.c lib/nspath.c lib/nsutils.c lib/pqueue.c \
	lib/rbtree.c lib/runcmd.c lib/snprintf.c lib/squeue.c lib/worker.c

check_PROGRAMS = test-bitmap test-dkhash test-fanout test-iobroker test-iocache \
	test-nsutils test-runcmd test-squeue

LIBTEST_UTILS = lib/t-utils.c lib/t-utils.h
test_bitmap_SOURCES = lib/test-bitmap.c $(LIBTEST_UTILS)
test_dkhash_SOURCES = lib/test-dkhash.c $(LIBTEST_UTILS)
test_fanout_SOURCES = lib/test-fanout.c $(LIBTEST_UTILS)
test_iobroker_SOURCES = lib/test-iobroker.c $(LIBTEST_UTILS)
test_iocache_SOURCES = lib/test-iocache.c $(LIBTEST_UTILS)
test_nsutils_SOURCES = lib/test-nsutils.c $(LIBTEST_UTILS)
test_runcmd_SOURCES = lib/test-runcmd.c $(LIBTEST_UTILS)
test_squeue_SOURCES = lib/test-squeue.c $(LIBTEST_UTILS)

COV_CFLAGS = -ggdb3 -O0 -ftest-coverage -fprofile-arcs -pg
cov-build:
	$(MAKE) CFLAGS='$(COV_CFLAGS)' LDFLAGS='$(COV_CFLAGS)' test

test: check

$(CONFIG_FILES) : % : %.in
	mkdir -p $(builddir)/$$(dirname $@)
	$(AM_V_GEN) sed \
		-e 's,@libdir[@],$(libdir),g' \
		-e 's,@pluginsdir[@],$(pluginsdir),g' \
		-e 's,@localstatedir[@],$(localstatedir),g' \
		-e 's,@lockfile[@],$(lockfile),g' \
		-e 's,@logdir[@],$(logdir),g' \
		-e 's,@naemon_grp[@],$(naemon_grp),g' \
		-e 's,@naemon_user[@],$(naemon_user),g' \
		-e 's,@pkgconfdir[@],$(pkgconfdir),g' \
		-e 's,@CHECKRESULTDIR[@],$(CHECKRESULTDIR),g' \
		-e 's,@TMPDIR[@],$(TMPDIR),g' \
		-e 's,@VERSION[@],$(VERSION),g' \
		-e 's,@MAIL_PROG[@],$(MAIL_PROG),g' \
		$< > $@


install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(CHECKRESULTDIR)
	$(mkinstalldirs) $(DESTDIR)$(logdir)
	$(mkinstalldirs) $(DESTDIR)$(logdir)/archives
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	[ -z $(logrotatedir) ] || $(install_sh_DATA) naemon.logrotate $(DESTDIR)$(logrotatedir)/naemon
	[ -z $(initrddir) ]    || $(install_sh_SCRIPT) daemon-init      $(DESTDIR)$(initrddir)/naemon
	if [ ! -d $(DESTDIR)$(pkgconfdir) ]; then \
		$(install_sh_PROGRAM) -d $(DESTDIR)$(pkgconfdir)/conf.d ; \
		$(install_sh_PROGRAM) -d $(DESTDIR)$(pkgconfdir)/conf.d/templates ; \
		$(install_sh_PROGRAM) -m 644 sample-config/naemon.cfg    $(DESTDIR)$(pkgconfdir)/naemon.cfg; \
		$(install_sh_PROGRAM) -m 640 sample-config/resource.cfg  $(DESTDIR)$(pkgconfdir)/resource.cfg; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/commands.cfg            $(DESTDIR)$(pkgconfdir)/conf.d/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/printer.cfg             $(DESTDIR)$(pkgconfdir)/conf.d/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/switch.cfg              $(DESTDIR)$(pkgconfdir)/conf.d/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/timeperiods.cfg         $(DESTDIR)$(pkgconfdir)/conf.d/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/localhost.cfg           $(DESTDIR)$(pkgconfdir)/conf.d/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/contacts.cfg            $(DESTDIR)$(pkgconfdir)/conf.d/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/windows.cfg             $(DESTDIR)$(pkgconfdir)/conf.d/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/templates/services.cfg  $(DESTDIR)$(pkgconfdir)/conf.d/templates/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/templates/hosts.cfg     $(DESTDIR)$(pkgconfdir)/conf.d/templates/ ; \
		$(install_sh_PROGRAM) -m 644 sample-config/conf.d/templates/contacts.cfg  $(DESTDIR)$(pkgconfdir)/conf.d/templates/ ; \
	fi


install-data-local:
	mkdir -p $(DESTDIR)$(mandir)/man8/
	install -m 644 naemon.8       $(DESTDIR)$(mandir)/man8/
	install -m 644 naemonstats.8  $(DESTDIR)$(mandir)/man8/
	install -m 644 shadownaemon.8 $(DESTDIR)$(mandir)/man8/

uninstall-hook:
	[ -z $(logrotatedir) ] || rm $(DESTDIR)$(logrotatedir)/naemon
	[ -z $(initrddir) ]    || rm $(DESTDIR)$(initrddir)/naemon
	rm $(DESTDIR)$(mandir)/man8/naemon.8
	rm $(DESTDIR)$(mandir)/man8/naemonstats.8
	rm $(DESTDIR)$(mandir)/man8/shadownaemon.8
	rm $(DESTDIR)$(pkgconfdir)/naemon.cfg
	rm $(DESTDIR)$(pkgconfdir)/resource.cfg
	rm -r $(DESTDIR)$(pkgconfdir)/conf.d

include tests.mk
TESTS = $(check_PROGRAMS)
TESTS += $(dist_check_SCRIPTS)

cov-report:
	@for f in $(TESTS) pqueue; do gcov -b -p $$f.c >/dev/null; done
	@rm -f *.h.gcov test-*.c.gcov
	@echo "--- untested functions:" | tee -a untested
	@grep '^function .* called 0' *.c.gcov \
		| sed -e 's/function \([^ ]*\) called 0.*/\1/' \
		| sed -e 's/\.c\.gcov:/\.c:/'

