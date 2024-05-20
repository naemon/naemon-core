check_PROGRAMS =

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

t_tap_test_config_SOURCES = t-tap/test_config.c
t_tap_test_config_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_config_LDADD = $(TAPLDADD)

t_tap_test_commands_SOURCES = t-tap/test_commands.c
t_tap_test_commands_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_commands_LDADD = $(TAPLDADD)

t_tap_test_downtime_SOURCES = t-tap/test_downtime.c
t_tap_test_downtime_CPPFLAGS = $(T_TAP_AM_CPPFLAGS)
t_tap_test_downtime_LDADD = $(TAPLDADD)

dist_check_SCRIPTS = t/705naemonstats.t t/900-configparsing.t
check_PROGRAMS += t-tap/test_macros t-tap/test_timeperiods t-tap/test_checks \
				  t-tap/test_config t-tap/test_commands t-tap/test_downtime
distclean-local:
	if test "${abs_srcdir}" != "${abs_builddir}"; then \
		rm -r t; \
		rm -r t-tap; \
	fi;

CLEANFILES += t-tap/smallconfig/naemon.log
EXTRA_DIST += t-tap/smallconfig/minimal.cfg t-tap/smallconfig/naemon.cfg \
	t-tap/smallconfig/resource.cfg t-tap/smallconfig/retention.dat
EXTRA_DIST += tests/configs/recursive tests/configs/services tests/configs/inc
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

tests_test_objects_SOURCE = tests/test-objects.c
tests_test_objects_LDADD =  $(TESTSLDADD)
tests_test_objects_LDFLAGS = $(TESTSLDFLAGS)
tests_test_objects_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_checks_SOURCES = tests/test-checks.c
tests_test_checks_LDADD =  $(TESTSLDADD)
tests_test_checks_LDFLAGS = $(TESTSLDFLAGS)
tests_test_checks_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_check_result_processing_SOURCES = tests/test-check-result-processing.c
tests_test_check_result_processing_LDADD = $(TESTSLDADD)
tests_test_check_result_processing_LDFLAGS = $(TESTSLDFLAGS)
tests_test_check_result_processing_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_scheduled_downtimes_SOURCES = tests/test-scheduled-downtimes.c
tests_test_scheduled_downtimes_LDADD = $(TESTSLDADD)
tests_test_scheduled_downtimes_LDFLAGS = $(TESTSLDFLAGS)
tests_test_scheduled_downtimes_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_check_scheduling_SOURCES = tests/test-check-scheduling.c
tests_test_check_scheduling_LDADD =  $(TESTSLDADD)
tests_test_check_scheduling_LDFLAGS = $(TESTSLDFLAGS)
tests_test_check_scheduling_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_check_dependencies_SOURCES = tests/test-check-dependencies.c
tests_test_check_dependencies_LDADD =  $(TESTSLDADD)
tests_test_check_dependencies_LDFLAGS = $(TESTSLDFLAGS)
tests_test_check_dependencies_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_query_handler_SOURCES = tests/test-query-handler.c
tests_test_query_handler_LDADD =  $(TESTSLDADD)
tests_test_query_handler_LDFLAGS = $(TESTSLDFLAGS)
tests_test_query_handler_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_arith_SOURCES = tests/test-arith.c
tests_test_arith_LDADD =  $(TESTSLDADD)
tests_test_arith_CFLAGS =  $(CFLAGS) -DNM_SKIP_BUILTIN_OVERFLOW_CHECKS=1
tests_test_arith_LDFLAGS = $(TESTSLDFLAGS)
tests_test_arith_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_arith_builtins_SOURCES = tests/test-arith.c
tests_test_arith_builtins_LDADD =  $(TESTSLDADD)
tests_test_arith_builtins_CFLAGS =  $(CFLAGS)
tests_test_arith_builtins_LDFLAGS = $(TESTSLDFLAGS)
tests_test_arith_builtins_CPPFLAGS = $(TESTSCPPFLAGS)

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

tests_test_external_command_nebcallback_SOURCES = tests/test-external-command-nebcallback.c
tests_test_external_command_nebcallback_LDADD = $(TESTSLDADD)
tests_test_external_command_nebcallback_LDFLAGS = $(TESTSLDFLAGS)
tests_test_external_command_nebcallback_CPPFLAGS = $(TESTSCPPFLAGS)

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

tests_test_worker_SOURCES = tests/test-worker.c
tests_test_worker_LDADD = $(TESTSLDADD)
tests_test_worker_LDFLAGS = $(TESTSLDADD)
tests_test_worker_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_retention_SOURCES = tests/test-retention.c
tests_test_retention_LDADD = $(TESTSLDADD)
tests_test_retention_LDFLAGS = $(TESTSLDADD)
tests_test_retention_CPPFLAGS = $(TESTSCPPFLAGS)

tests_test_neb_callbacks_SOURCES = tests/test-neb-callbacks.c
tests_test_neb_callbacks_LDADD = $(TESTSLDADD)
tests_test_neb_callbacks_LDFLAGS = $(TESTSLDADD)
tests_test_neb_callbacks_CPPFLAGS = $(TESTSCPPFLAGS)

check_PROGRAMS += \
	tests/test-neb-callbacks \
	tests/test-checks \
	tests/test-check-result-processing \
	tests/test-scheduled-downtimes \
	tests/test-check-scheduling \
	tests/test-check-dependencies \
	tests/test-query-handler \
	tests/test-obj-config-parse \
	tests/test-utils \
	tests/test-log \
	tests/test-config \
	tests/test-event-heap \
	tests/test-external-command-nebcallback \
	tests/test-kv-command \
	tests/test-kvvec \
	tests/test-objects \
	tests/test-kvvec-ekvstr \
	tests/test-worker \
	tests/test-retention \
	tests/test-arith \
	tests/test-arith-builtins

LIBTEST_UTILS = lib/t-utils.c lib/t-utils.h
test_bitmap_SOURCES = lib/test-bitmap.c $(LIBTEST_UTILS)
test_iobroker_SOURCES = lib/test-iobroker.c $(LIBTEST_UTILS)
test_bufferqueue_SOURCES = lib/test-bufferqueue.c $(LIBTEST_UTILS)
test_nsutils_SOURCES = lib/test-nsutils.c $(LIBTEST_UTILS)
test_runcmd_SOURCES = lib/test-runcmd.c $(LIBTEST_UTILS)
check_PROGRAMS += test-bitmap test-iobroker test-bufferqueue \
	test-nsutils test-runcmd


endif
TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	build-aux/tap-driver.sh
