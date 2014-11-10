EXTRA_DIST = configs/recursive configs/services configs/includes
if HAVE_CHECK
LDADD = @CHECK_LIBS@ $(top_builddir)/naemon/lib/libnaemon.la -lm -ldl
AM_CPPFLAGS += -I$(top_srcdir) '-DSYSCONFDIR="$(abs_srcdir)/configs/"' -DNAEMON_COMPILATION
AM_CFLAGS += @CHECK_CFLAGS@
TESTS = test-checks test-utils test-log test-config
GENERAL_DEPS = nebmods.o commands.o broker.o query-handler.o utils.o events.o notifications.o \
			  flapping.o sehandlers.o workers.o shared.o comments.o downtime.o sretention.o objects.o \
			  macros.o statusdata.o xrddefault.o xsddefault.o xpddefault.o perfdata.o xodtemplate.o nm_alloc.o

TEST_CHECKS_DEPS = $(GENERAL_DEPS) logging.o
test_checks_SOURCES	= test-checks.c $(top_srcdir)/naemon/checks.h $(top_srcdir)/naemon/checks.c $(top_srcdir)/naemon/defaults.c
test_checks_LDADD =  $(TEST_CHECKS_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)

TEST_UTILS_DEPS = $(GENERAL_DEPS) checks.o logging.o
test_utils_SOURCES	= test-utils.c $(top_srcdir)/naemon/defaults.c
test_utils_LDADD =  $(TEST_UTILS_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)

TEST_LOG_DEPS = $(GENERAL_DEPS) checks.o
test_log_SOURCES	= test-log.c $(top_srcdir)/naemon/defaults.c
test_log_LDADD = $(TEST_LOG_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)

TEST_CONFIG_DEPS = $(GENERAL_DEPS) checks.o configuration.o logging.o
test_config_SOURCES	= test-config.c $(top_srcdir)/naemon/defaults.c
test_config_LDADD = $(TEST_CONFIG_DEPS:%=$(top_builddir)/naemon/naemon-%) $(LDADD)

check_PROGRAMS = $(TESTS)
endif
