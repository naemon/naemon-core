SUBDIRS = lib
AM_CPPFLAGS += -I$(top_builddir)
BUILT_SOURCES = wpres-phash.h

wpres-phash.h: wpres.gperf
	$(GPERF) --switch=1 --struct-type \
		--hash-function-name=wpres_key_phash \
		--lookup-function-name=wpres_get_key \
	$< > $@

CLEANFILES = wpres-phash.h

bin_PROGRAMS = naemon
naemon_SOURCES = \
	broker.c broker.h \
	checks.c checks.h \
	commands.c commands.h \
	comments.c comments.h \
	common.h \
	config.c config.h \
	defaults.h \
	downtime.c downtime.h \
	events.c events.h \
	flapping.c flapping.h \
	logging.c logging.h \
	macros.c macros.h \
	naemon.c naemon.h \
	nebcallbacks.h neberrors.h \
	nebmods.c nebmods.h \
	nebmodules.h nebstructs.h \
	nerd.c nerd.h \
	notifications.c notifications.h \
	objects.c objects.h \
	perfdata.c perfdata.h \
	query-handler.c query-handler.h \
	sehandlers.c \
	shared.c shared.h \
	sretention.c sretention.h \
	statusdata.c statusdata.h \
	utils.c utils.h \
	workers.c workers.h \
	xcddefault.c xcddefault.h \
	xodtemplate.c xodtemplate.h \
	xpddefault.c xpddefault.h \
	xrddefault.c xrddefault.h \
	xsddefault.c xsddefault.h
naemon_CPPFLAGS=$(AM_CPPFLAGS) -DPREFIX='"$(prefix)"'
naemon_LDADD=\
	lib/libnaemon.a \
	-lm -ldl
naemon_LDFLAGS=-rdynamic

#naemonworker_SOURCES=
#naemonworker_LDADD=$(top_builddir)/naemon/worker/libnaemon_worker.a \
#	$(top_builddir)/naemon/lib/libnaemon.a

distclean-local:
	rm -rf Makefile.in
