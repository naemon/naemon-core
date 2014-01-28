SUBDIRS = lib
AM_CPPFLAGS += -I$(top_builddir) '-DSYSCONFDIR="$(sysconfdir)"' '-DLOCALSTATEDIR="$(localstatedir)"' '-DLOGDIR="$(logdir)"' '-DLOCKFILE="$(lockfile)"'
BUILT_SOURCES = wpres-phash.h

wpres-phash.h: wpres.gperf
	$(AM_V_GEN) $(GPERF) --switch=1 --struct-type \
		--hash-function-name=wpres_key_phash \
		--lookup-function-name=wpres_get_key \
		--language=ANSI-C \
	$< > $@

CLEANFILES = wpres-phash.h

bin_PROGRAMS = naemon
naemon_SOURCES = \
	broker.c broker.h \
	checks.c checks.h \
	commands.c commands.h \
	comments.c comments.h \
	common.h \
	configuration.c configuration.h \
	defaults.h \
	downtime.c downtime.h \
	events.c events.h \
	flapping.c flapping.h \
	globals.h \
	loadctl.h \
	logging.c logging.h \
	macros.c macros.h \
	naemon.c \
	nebcallbacks.h neberrors.h \
	nebmods.c nebmods.h \
	nebmodules.h nebstructs.h \
	nerd.c nerd.h \
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
	xcddefault.c xcddefault.h \
	xodtemplate.c xodtemplate.h \
	xpddefault.c xpddefault.h \
	xrddefault.c xrddefault.h \
	xsddefault.c xsddefault.h \
	nagios.h naemon.h
naemon_CPPFLAGS=$(AM_CPPFLAGS) -DPREFIX='"$(prefix)"'
naemon_LDADD=\
	lib/libnaemon.la \
	-lm -ldl
naemon_LDFLAGS=-rdynamic -static

#naemonworker_SOURCES=
#naemonworker_LDADD=$(top_builddir)/naemon/worker/libnaemon_worker.a \
#	$(top_builddir)/naemon/lib/libnaemon.a

distclean-local:
	rm -rf Makefile.in
