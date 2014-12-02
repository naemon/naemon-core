SUBDIRS = lib
AM_CPPFLAGS += -I$(top_builddir) -DNAEMON_COMPILATION
BUILT_SOURCES = wpres-phash.h buildopts.h
EXTRA_DIST = buildopts.h.in

nobase_pkginclude_HEADERS = \
	lib/bitmap.h    lib/kvvec.h       lib/nsock.h    lib/skiplist.h \
	lib/dkhash.h    lib/libnaemon.h   lib/nspath.h   lib/snprintf.h \
	lib/fanout.h    lib/libnagios.h   lib/nsutils.h  lib/squeue.h \
	lib/iobroker.h  lib/lnae-utils.h  lib/pqueue.h   lib/t-utils.h \
	lib/iocache.h   lib/lnag-utils.h  lib/runcmd.h   lib/worker.h \
	lib/rbtree.h

pkginclude_HEADERS = \
	broker.h         events.h       nagios.h         objects.h \
	workers.h \
	checks.h         flapping.h     nebcallbacks.h   perfdata.h \
	commands.h       globals.h      neberrors.h      query-handler.h \
	comments.h       loadctl.h      nebmods.h        sehandlers.h \
	common.h         logging.h      nebmodules.h     shared.h \
	configuration.h  macros.h       nebstructs.h     sretention.h \
	defaults.h       naemon.h       nerd.h           statusdata.h \
	downtime.h       naemonstats.h  notifications.h  utils.h \
	buildopts.h      nm_alloc.h

all-local: manpages

wpres-phash.h: wpres.gperf
	$(AM_V_GEN) $(GPERF) --switch=1 --struct-type \
		--hash-function-name=wpres_key_phash \
		--lookup-function-name=wpres_get_key \
		--language=ANSI-C \
	$< > $@

buildopts.h: buildopts.h.in
	sed -e 's,@@NAEMON_SYSCONFDIR@@,$(pkgconfdir),' \
	 -e 's,@@NAEMON_LOCALSTATEDIR@@,$(localstatedir),' \
	 -e 's,@@NAEMON_LOGDIR@@,$(logdir),' \
	 -e 's,@@NAEMON_LOCKFILE@@,$(lockfile),' \
	$< > $@

CLEANFILES = wpres-phash.h buildopts.h naemon.8 naemonstats.8 shadownaemon.8 oconfsplit.8

common_sources = \
	broker.c broker.h \
	checks.c checks.h \
	commands.c commands.h \
	comments.c comments.h \
	common.h \
	configuration.c configuration.h \
	defaults.c defaults.h \
	downtime.c downtime.h \
	events.c events.h \
	flapping.c flapping.h \
	globals.h \
	loadctl.h \
	logging.c logging.h \
	macros.c macros.h \
	nebcallbacks.h neberrors.h \
	nebmods.c nebmods.h \
	nebmodules.h nebstructs.h \
	nerd.c nerd.h \
	nm_alloc.c nm_alloc.h \
	notifications.c notifications.h \
	objects.c objects.h \
	perfdata.c perfdata.h \
	query-handler.c query-handler.h \
	sehandlers.c sehandlers.h \
	shared.c shared.h \
	sretention.c sretention.h \
	statusdata.c statusdata.h \
	utils.c utils.h \
	workers.c workers.h \
	xodtemplate.c xodtemplate.h \
	xpddefault.c xpddefault.h \
	xrddefault.c xrddefault.h \
	xsddefault.c xsddefault.h \
	nagios.h naemon.h \
	wpres.gperf \
	buildopts.h


bin_PROGRAMS = naemon naemonstats oconfsplit shadownaemon
naemon_SOURCES = naemon.c $(common_sources)

naemon_CPPFLAGS = $(AM_CPPFLAGS) -DPREFIX='"$(prefix)"'
naemon_LDADD = lib/libnaemon.la -lm -ldl
naemon_LDFLAGS = -rdynamic -static

naemonstats_SOURCES = naemonstats.c buildopts.h lib/nspath.h lib/nspath.c defaults.h defaults.c

shadownaemon_SOURCES = shadownaemon.c shadownaemon.h $(common_sources)
shadownaemon_LDADD = lib/libnaemon.la -lm -ldl
shadownaemon_LDFLAGS = -rdynamic -static

oconfsplit_LDADD = lib/libnaemon.la -lm -ldl
oconfsplit_SOURCES = oconfsplit.c $(common_sources)

#naemonworker_SOURCES=
#naemonworker_LDADD=$(top_builddir)/naemon/worker/libnaemon_worker.a \
#	$(top_builddir)/naemon/lib/libnaemon.a

distclean-local:
	rm -rf Makefile.in

manpages: naemon naemonstats shadownaemon oconfsplit
	$(HELP2MAN) --no-info --section=8 --help-option=-h -n "monitoring core"                          ./naemon       > naemon.8
	$(HELP2MAN) --no-info --section=8 --help-option=-h -n "gather statistics from naemon core"       ./naemonstats  > naemonstats.8
	$(HELP2MAN) --no-info --section=8 --help-option=-h -n "shadow remote cores via livestatus"       ./shadownaemon > shadownaemon.8
	$(HELP2MAN) --no-info --section=8 --help-option=-h -n "split naemon configuration by hostgroups" ./oconfsplit   > oconfsplit.8

install-data-local:
	mkdir -p $(DESTDIR)$(mandir)/man8/
	install -m 644 naemon.8       $(DESTDIR)$(mandir)/man8/
	install -m 644 naemonstats.8  $(DESTDIR)$(mandir)/man8/
	install -m 644 shadownaemon.8 $(DESTDIR)$(mandir)/man8/
	install -m 644 oconfsplit.8   $(DESTDIR)$(mandir)/man8/

uninstall-hook:
	rm $(DESTDIR)$(mandir)/man8/naemon.8
	rm $(DESTDIR)$(mandir)/man8/naemonstats.8
	rm $(DESTDIR)$(mandir)/man8/shadownaemon.8
	rm $(DESTDIR)$(mandir)/man8/oconfsplit.8
