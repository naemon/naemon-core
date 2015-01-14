BROKEN = test_downtime test_nagios_config test_xsddefault

T_TAP_AM_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_srcdir)/tap/src -DNAEMON_BUILDOPTS_H__ '-DNAEMON_SYSCONFDIR="$(abs_builddir)/t-tap/smallconfig/"' '-DNAEMON_LOCALSTATEDIR="$(abs_builddir)/t-tap/"' '-DNAEMON_LOGDIR="$(abs_builddir)/t-tap/"' '-DNAEMON_LOCKFILE="$(lockfile)"' -DNAEMON_COMPILATION
T_TAP_LDADD = -ltap -L$(top_builddir)/tap/src -L$(top_builddir)/lib -lnaemon -ldl -lm
BASE_DEPS = libnaemon.la
TAP_DEPS = $(BASE_DEPS) tap/src/libtap.la
BASE_SOURCE = \
	src/naemon/broker.c src/naemon/checks.c src/naemon/checks_host.c \
	src/naemon/checks_service.c src/naemon/comments.c \
	src/naemon/defaults.c src/naemon/downtime.c \
	src/naemon/flapping.c src/naemon/macros.c \
	src/naemon/nebmods.c src/naemon/nm_alloc.c src/naemon/notifications.c \
	src/naemon/objects.c src/naemon/objectlist.c src/naemon/objects_common.c \
	src/naemon/objects_command.c src/naemon/objects_contact.c \
	src/naemon/objects_contactgroup.c src/naemon/objects_host.c \
	src/naemon/objects_hostgroup.c \
	src/naemon/objects_hostdependency.c src/naemon/objects_service.c \
	src/naemon/objects_servicegroup.c src/naemon/objects_timeperiod.c \
	src/naemon/perfdata.c src/naemon/query-handler.c \
	src/naemon/sehandlers.c src/naemon/shared.c src/naemon/sretention.c \
	src/naemon/statusdata.c src/naemon/workers.c src/naemon/xodtemplate.c \
	src/naemon/xpddefault.c src/naemon/xrddefault.c src/naemon/xsddefault.c \
	src/naemon/buildopts.h src/naemon/wpres-phash.h

t_tap_test_timeperiods_SOURCES = t-tap/test_timeperiods.c $(BASE_SOURCE) src/naemon/configuration.c  src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
t_tap_test_timeperiods_LDADD = $(T_TAP_LDADD)
t_tap_test_timeperiods_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_timeperiods_DEPENDENCIES = $(TAP_DEPS)

t_tap_test_macros_SOURCES = t-tap/test_macros.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
t_tap_test_macros_LDADD = $(T_TAP_LDADD)
t_tap_test_macros_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_macros_DEPENDENCIES = $(TAP_DEPS)

t_tap_test_checks_SOURCES = t-tap/test_checks.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
t_tap_test_checks_LDADD = $(T_TAP_LDADD)
t_tap_test_checks_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_checks_DEPENDENCIES = $(TAP_DEPS)

t_tap_test_neb_callbacks_SOURCES = t-tap/test_neb_callbacks.c t-tap/fixtures.c t-tap/fixtures.h $(BASE_SOURCE) src/naemon/utils.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
t_tap_test_neb_callbacks_LDADD = $(T_TAP_LDADD)
t_tap_test_neb_callbacks_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_neb_callbacks_DEPENDENCIES = $(TAP_DEPS)

t_tap_test_config_SOURCES = t-tap/test_config.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/configuration.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
t_tap_test_config_LDADD = $(T_TAP_LDADD)
t_tap_test_config_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_config_DEPENDENCIES = $(TAP_DEPS)

t_tap_test_commands_SOURCES = t-tap/test_commands.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/configuration.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
t_tap_test_commands_LDADD = $(T_TAP_LDADD)
t_tap_test_commands_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_commands_DEPENDENCIES = $(TAP_DEPS)

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
TESTS_LDADD = @CHECK_LIBS@ libnaemon.la -Llib -lm -ldl
TESTS_LDFLAGS = -static
TESTS_AM_CPPFLAGS = $(AM_CPPFLAGS) -Isrc '-DSYSCONFDIR="$(abs_srcdir)/tests/configs/"' -DNAEMON_COMPILATION
AM_CFLAGS += @CHECK_CFLAGS@

tests_test_checks_SOURCES = tests/test-checks.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
tests_test_checks_LDADD = $(TESTS_LDADD)
tests_test_checks_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_checks_CPPFLAGS = $(TESTS_AM_CPPFLAGS)
tests_test_checks_DEPENDENCIES = $(BASE_DEPS)

tests_test_utils_SOURCES = tests/test-utils.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
tests_test_utils_LDADD =  $(TESTS_LDADD)
tests_test_utils_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_utils_CPPFLAGS = $(TESTS_AM_CPPFLAGS)
tests_test_utils_DEPENDENCIES = $(BASE_DEPS)

tests_test_log_SOURCES = tests/test-log.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/events.c src/naemon/commands.c
tests_test_log_LDADD = $(TESTS_LDADD)
tests_test_log_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_log_CPPFLAGS = $(TESTS_AM_CPPFLAGS)
tests_test_log_DEPENDENCIES = $(BASE_DEPS)

tests_test_config_SOURCES = tests/test-config.c $(BASE_SOURCE) src/naemon/utils.c src/naemon/configuration.c src/naemon/logging.c src/naemon/events.c src/naemon/commands.c
tests_test_config_LDADD = $(TESTS_LDADD)
tests_test_config_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_config_CPPFLAGS = $(TESTS_AM_CPPFLAGS)
tests_test_config_DEPENDENCIES = $(BASE_DEPS)

tests_test_event_heap_SOURCES = tests/test-event-heap.c $(BASE_SOURCE) src/naemon/logging.c src/naemon/utils.c src/naemon/commands.c
tests_test_event_heap_LDADD = $(TESTS_LDADD)
tests_test_event_heap_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_event_heap_CPPFLAGS = $(TESTS_AM_CPPFLAGS)
tests_test_event_heap_DEPENDENCIES = $(BASE_DEPS)

tests_test_kv_command_SOURCES = tests/test-kv-command.c $(BASE_SOURCE) src/naemon/logging.c src/naemon/utils.c src/naemon/events.c
tests_test_kv_command_LDADD = $(TESTS_LDADD)
tests_test_kv_command_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_kv_command_CPPFLAGS = $(TESTS_AM_CPPFLAGS)
tests_test_kv_command_DEPENDENCIES = $(BASE_DEPS)

tests_test_kvvec_ekvstr_SOURCES = tests/test-kvvec-ekvstr.c
tests_test_kvvec_ekvstr_LDADD = $(TESTS_LDADD)
tests_test_kvvec_ekvstr_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_kvvec_ekvstr_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

tests_test_kvvec_SOURCES = tests/test-kvvec.c
tests_test_kvvec_LDADD = $(TESTS_LDADD)
tests_test_kvvec_LDFLAGS = $(TESTS_LDFLAGS)
tests_test_kvvec_CPPFLAGS = $(TESTS_AM_CPPFLAGS)

check_PROGRAMS += \
	tests/test-checks \
	tests/test-utils \
	tests/test-log \
	tests/test-config \
	tests/test-event-heap \
	tests/test-kv-command \
	tests/test-kvvec \
	tests/test-kvvec-ekvstr


endif
TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	build-aux/tap-driver.sh
