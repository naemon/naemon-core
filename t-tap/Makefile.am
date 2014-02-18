BROKEN = test_downtime test_events test_nagios_config test_xsddefault

AM_CPPFLAGS += -I$(top_srcdir) -I$(top_srcdir)/tap/src -I$(top_builddir) '-DSYSCONFDIR="$(abs_srcdir)/smallconfig/"' '-DLOCALSTATEDIR="$(localstatedir)"' '-DLOGDIR="$(abs_builddir)/"' '-DLOCKFILE="$(lockfile)"'
AM_CFLAGS += -Wno-error -ggdb3
LDADD = -ltap -L$(top_builddir)/tap/src -lnaemon -L$(top_builddir)/naemon/lib -ldl -lm
BASE_DEPS = broker.o checks.o commands.o comments.o \
	configuration.o downtime.o events.o flapping.o logging.o \
	macros.o nebmods.o notifications.o objects.o perfdata.o \
	query-handler.o sehandlers.o shared.o sretention.o statusdata.o \
	workers.o xcddefault.o xodtemplate.o xpddefault.o xrddefault.o \
	xsddefault.o
TIMEPERIODS_DEPS = $(BASE_DEPS)
MACROS_DEPS = $(BASE_DEPS) utils.o
CHECKS_DEPS = $(BASE_DEPS) utils.o
NEB_CALLBACKS_DEPS = $(BASE_DEPS) utils.o
CONFIG_DEPS = $(BASE_DEPS) utils.o
test_timeperiods_LDADD = $(TIMEPERIODS_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)
test_macros_LDADD = $(MACROS_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)
test_checks_LDADD = $(CHECKS_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)
test_neb_callbacks_SOURCES = test_neb_callbacks.c fixtures.c fixtures.h
test_neb_callbacks_LDADD = $(NEB_CALLBACKS_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)
test_config_LDADD = $(CONFIG_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)
check_PROGRAMS = test_macros test_timeperiods test_checks \
	test_neb_callbacks test_config
TESTS = $(check_PROGRAMS)
EXTRA_DIST = smallconfig/minimal.cfg smallconfig/naemon.cfg \
	smallconfig/resource.cfg smallconfig/retention.dat
