BROKEN = test_downtime test_nagios_config test_xsddefault

AM_CFLAGS += -Wno-error

T_TAP_AM_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_srcdir)/tap/src -DNAEMON_BUILDOPTS_H__ '-DNAEMON_SYSCONFDIR="$(abs_builddir)/t-tap/smallconfig/"' '-DNAEMON_LOCALSTATEDIR="$(abs_builddir)/t-tap/"' '-DNAEMON_LOGDIR="$(abs_builddir)/t-tap/"' '-DNAEMON_LOCKFILE="$(lockfile)"' -DNAEMON_COMPILATION
T_TAP_LDADD = -ltap -L$(top_builddir)/tap/src -L$(top_builddir)/lib -lnaemon -ldl -lm
BASE_DEPS = broker.o checks.o checks_host.o checks_service.o commands.o comments.o \
	configuration.o downtime.o events.o flapping.o logging.o \
	macros.o nebmods.o notifications.o objects.o perfdata.o \
	query-handler.o sehandlers.o shared.o sretention.o statusdata.o \
	workers.o xodtemplate.o xpddefault.o xrddefault.o \
	xsddefault.o nm_alloc.o
TIMEPERIODS_DEPS = $(BASE_DEPS)
MACROS_DEPS = $(BASE_DEPS) utils.o
CHECKS_DEPS = $(BASE_DEPS) utils.o
NEB_CALLBACKS_DEPS = $(BASE_DEPS) utils.o
CONFIG_DEPS = $(BASE_DEPS) utils.o
COMMANDS_DEPS = $(BASE_DEPS) utils.o
t_tap_test_timeperiods_SOURCES = t-tap/test_timeperiods.c src/naemon/defaults.c
t_tap_test_timeperiods_LDADD = $(TIMEPERIODS_DEPS:%=$(top_builddir)/src/naemon/%) $(T_TAP_LDADD)
t_tap_test_timeperiods_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_macros_SOURCES = t-tap/test_macros.c src/naemon/defaults.c
t_tap_test_macros_LDADD = $(MACROS_DEPS:%=$(top_builddir)/src/naemon/%) $(T_TAP_LDADD)
t_tap_test_macros_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_checks_SOURCES = t-tap/test_checks.c src/naemon/defaults.c
t_tap_test_checks_LDADD = $(CHECKS_DEPS:%=$(top_builddir)/src/naemon/%) $(T_TAP_LDADD)
t_tap_test_checks_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_neb_callbacks_SOURCES = t-tap/test_neb_callbacks.c t-tap/fixtures.c t-tap/fixtures.h src/naemon/defaults.c
t_tap_test_neb_callbacks_LDADD = $(NEB_CALLBACKS_DEPS:%=$(top_builddir)/src/naemon/%) $(T_TAP_LDADD)
t_tap_test_neb_callbacks_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_config_SOURCES = t-tap/test_config.c src/naemon/defaults.c
t_tap_test_config_LDADD = $(CONFIG_DEPS:%=$(top_builddir)/src/naemon/%) $(T_TAP_LDADD)
t_tap_test_config_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_commands_SOURCES = t-tap/test_commands.c src/naemon/defaults.c
t_tap_test_commands_LDADD = $(COMMANDS_DEPS:%=$(top_builddir)/src/naemon/%) $(T_TAP_LDADD)
t_tap_test_commands_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
dist_check_SCRIPTS = t/705naemonstats.t t/900-configparsing.t t/910-noservice.t t/920-nocontactgroup.t t/930-emptygroups.t
check_PROGRAMS += t-tap/test_macros t-tap/test_timeperiods t-tap/test_checks \
	t-tap/test_neb_callbacks t-tap/test_config t-tap/test_commands
distclean-local:
	if test "${abs_srcdir}" != "${abs_builddir}"; then \
		rm -r t; \
		rm -r t-tap; \
	fi;

CLEANFILES += t-tap/smallconfig/naemon.log
EXTRA_DIST += t-tap/smallconfig/minimal.cfg t-tap/smallconfig/naemon.cfg \
	t-tap/smallconfig/resource.cfg t-tap/smallconfig/retention.dat
EXTRA_DIST += tests/configs/recursive tests/configs/services tests/configs/includes
EXTRA_DIST += $(dist_check_SCRIPTS)
EXTRA_DIST += t/etc/* t/var/*
TESTS_ENVIRONMENT = \
					   if test "${abs_srcdir}" != "${abs_builddir}"; then \
					   mkdir -p ${abs_builddir}/t-tap; \
					   mkdir -p ${abs_builddir}/t; \
					   cp -R ${abs_srcdir}/t-tap/smallconfig "${abs_builddir}/t-tap/"; \
					   cp -R ${abs_srcdir}/t/etc "${abs_builddir}/t/"; \
					   cp -R ${abs_srcdir}/t/var "${abs_builddir}/t/"; \
					   chmod -R u+w ${abs_builddir}/t/etc ${abs_builddir}/t/var \
							${abs_builddir}/t-tap/smallconfig; \
					   fi; \
					   builddir=$(abs_builddir); export builddir;
if HAVE_CHECK
TESTS_LDADD = @CHECK_LIBS@ -Llib -lnaemon -lm -ldl
TESTS_AM_CPPFLAGS = $(AM_CPPFLAGS) -Isrc '-DSYSCONFDIR="$(abs_srcdir)/tests/configs/"' -DNAEMON_COMPILATION
AM_CFLAGS += @CHECK_CFLAGS@
GENERAL_DEPS = nebmods.o commands.o broker.o query-handler.o utils.o events.o notifications.o \
			  flapping.o sehandlers.o workers.o shared.o comments.o downtime.o sretention.o objects.o \
			  macros.o statusdata.o xrddefault.o xsddefault.o xpddefault.o perfdata.o xodtemplate.o nm_alloc.o

TEST_CHECKS_DEPS = $(GENERAL_DEPS) logging.o
tests_test_checks_SOURCES	= tests/test-checks.c \
	src/naemon/checks.h src/naemon/checks.c \
	src/naemon/checks_host.h src/naemon/checks_host.c \
	src/naemon/checks_service.h src/naemon/checks_service.c \
	src/naemon/defaults.c
tests_test_checks_LDADD =  $(TEST_CHECKS_DEPS:%=$(top_builddir)/src/naemon/%) $(TESTS_LDADD)
tests_test_checks_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

TEST_UTILS_DEPS = $(GENERAL_DEPS) checks.o checks_host.o checks_service.o logging.o
tests_test_utils_SOURCES	= tests/test-utils.c src/naemon/defaults.c
tests_test_utils_LDADD =  $(TEST_UTILS_DEPS:%=$(top_builddir)/src/naemon/%) $(TESTS_LDADD)
tests_test_utils_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

TEST_LOG_DEPS = $(GENERAL_DEPS) checks.o checks_host.o checks_service.o
tests_test_log_SOURCES	= tests/test-log.c src/naemon/defaults.c
tests_test_log_LDADD = $(TEST_LOG_DEPS:%=$(top_builddir)/src/naemon/%) $(TESTS_LDADD)
tests_test_log_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

TEST_CONFIG_DEPS = $(GENERAL_DEPS) checks.o checks_host.o checks_service.o configuration.o logging.o
tests_test_config_SOURCES	= tests/test-config.c src/naemon/defaults.c
tests_test_config_LDADD = $(TEST_CONFIG_DEPS:%=$(top_builddir)/src/naemon/%) $(TESTS_LDADD)
tests_test_config_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

TEST_EVENT_HEAP_DEPS = $(filter-out events.o,$(GENERAL_DEPS)) checks.o checks_host.o checks_service.o logging.o
tests_test_event_heap_SOURCES	= tests/test-event-heap.c src/naemon/defaults.c
tests_test_event_heap_LDADD = $(TEST_EVENT_HEAP_DEPS:%=$(top_builddir)/src/naemon/%) $(TESTS_LDADD)
tests_test_event_heap_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

TEST_KVVEC_EKVSTR_DEPS =
tests_test_kvvec_ekvstr_SOURCES	= tests/test-kvvec-ekvstr.c
tests_test_kvvec_ekvstr_LDADD = $(TEST_KVVEC_EKVSTR_DEPS:%=$(top_builddir)/src/naemon/%) $(TESTS_LDADD)
tests_test_kvvec_ekvstr_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

TEST_KVVEC_DEPS =
tests_test_kvvec_SOURCES = tests/test-kvvec.c
tests_test_kvvec_LDADD = $(TEST_KVVEC_DEPS:%=$(top_builddir)/src/naemon/%) $(TESTS_LDADD)
tests_test_kvvec_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

check_PROGRAMS += \
	tests/test-checks \
	tests/test-utils \
	tests/test-log \
	tests/test-config \
	tests/test-event-heap \
	tests/test-kvvec \
	tests/test-kvvec-ekvstr


endif
TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	build-aux/tap-driver.sh
