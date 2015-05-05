T_TAP_AM_CPPFLAGS = $(AM_CPPFLAGS) $(GLIB_CFLAGS) -I$(abs_srcdir)/tap/src -DTESTDIR='"$(abs_builddir)/t-tap/smallconfig/"'
BASE_DEPS = libnaemon.la
TAPLDADD = $(LDADD) tap/src/libtap.la

t_tap_test_timeperiods_SOURCES = t-tap/test_timeperiods.c
t_tap_test_timeperiods_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_timeperiods_LDADD = $(TAPLDADD)

t_tap_test_macros_SOURCES = t-tap/test_macros.c
t_tap_test_macros_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_macros_LDADD = $(TAPLDADD)

t_tap_test_checks_SOURCES = t-tap/test_checks.c
t_tap_test_checks_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_checks_LDADD = $(TAPLDADD)

t_tap_test_neb_callbacks_SOURCES = t-tap/test_neb_callbacks.c t-tap/fixtures.c t-tap/fixtures.h
t_tap_test_neb_callbacks_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_neb_callbacks_LDADD = $(TAPLDADD)

t_tap_test_config_SOURCES = t-tap/test_config.c
t_tap_test_config_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_config_LDADD = $(TAPLDADD)

t_tap_test_commands_SOURCES = t-tap/test_commands.c
t_tap_test_commands_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_commands_LDADD = $(TAPLDADD)

t_tap_test_downtime_SOURCES = t-tap/test_downtime.c
t_tap_test_downtime_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_downtime_LDADD = $(TAPLDADD)

dist_check_SCRIPTS = t/705naemonstats.t t/900-configparsing.t t/910-noservice.t t/920-nocontactgroup.t t/930-emptygroups.t
check_PROGRAMS += t-tap/test_macros t-tap/test_timeperiods t-tap/test_checks \
	t-tap/test_neb_callbacks t-tap/test_config t-tap/test_commands t-tap/test_downtime
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
					   builddir=$(abs_builddir); export builddir; \
					   G_DEBUG=fatal-criticals; export G_DEBUG;

if HAVE_CHECK
TESTSLDADD = $(LDADD) $(CHECK_LIBS) $(GLIB_LIBS)
TESTSCPPFLAGS = $(AM_CPPFLAGS) -Isrc -DTESTDIR='"$(abs_srcdir)/tests/configs/"'

tests_test_checks_SOURCES = tests/test-checks.c
tests_test_checks_LDADD =  $(TESTSLDADD)
tests_test_checks_LDFLAGS = $(TESTSLDFLAGS)
tests_test_checks_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_obj_config_parse_SOURCES = tests/test-obj-config-parse.c
tests_test_obj_config_parse_LDADD =  $(TESTSLDADD)
tests_test_obj_config_parse_LDFLAGS = $(TESTSLDFLAGS)
tests_test_obj_config_parse_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_utils_SOURCES = tests/test-utils.c
tests_test_utils_LDADD =  $(TESTSLDADD)
tests_test_utils_LDFLAGS = $(TESTSLDFLAGS)
tests_test_utils_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_log_SOURCES = tests/test-log.c
tests_test_log_LDADD = $(TESTSLDADD)
tests_test_log_LDFLAGS = $(TESTSLDFLAGS)
tests_test_log_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_config_SOURCES = tests/test-config.c
tests_test_config_LDADD = $(TESTSLDADD)
tests_test_config_LDFLAGS = $(TESTSLDFLAGS)
tests_test_config_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_event_heap_SOURCES = tests/test-event-heap.c
tests_test_event_heap_LDADD = $(TESTSLDADD)
tests_test_event_heap_LDFLAGS = $(TESTSLDFLAGS)
tests_test_event_heap_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_kv_command_SOURCES = tests/test-kv-command.c
tests_test_kv_command_LDADD = $(TESTSLDADD)
tests_test_kv_command_LDFLAGS = $(TESTSLDFLAGS)
tests_test_kv_command_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_kvvec_ekvstr_SOURCES = tests/test-kvvec-ekvstr.c
tests_test_kvvec_ekvstr_LDADD = $(TESTSLDADD)
tests_test_kvvec_ekvstr_LDFLAGS = $(TESTSLDFLAGS)
tests_test_kvvec_ekvstr_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_kvvec_SOURCES = tests/test-kvvec.c
tests_test_kvvec_LDADD = $(TESTSLDADD)
tests_test_kvvec_LDFLAGS = $(TESTSLDFLAGS)
tests_test_kvvec_CPPFLAGS = $(TESTSCPPFLAGS)

check_PROGRAMS += \
	tests/test-checks \
	tests/test-obj-config-parse \
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
