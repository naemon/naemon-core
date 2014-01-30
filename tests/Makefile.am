if HAVE_CHECK
AM_CPPFLAGS = -I$(top_builddir)
TESTS = test-checks test-log
TEST_CHECKS_DEPS = nebmods.o commands.o broker.o query-handler.o utils.o events.o notifications.o \
			  flapping.o sehandlers.o logging.o workers.o shared.o comments.o downtime.o sretention.o objects.o \
			  macros.o statusdata.o xcddefault.o xrddefault.o xsddefault.o xpddefault.o perfdata.o xodtemplate.o
test_checks_SOURCES	= test-checks.c $(top_builddir)/naemon/checks.h $(top_builddir)/naemon/checks.c
test_checks_CFLAGS = @CHECK_CFLAGS@
test_checks_LDADD =  $(TEST_CHECKS_DEPS:%=$(top_builddir)/naemon/naemon-%) @CHECK_LIBS@ $(top_builddir)/naemon/lib/libnaemon.la -lm -ldl
TEST_LOG_DEPS = $(TEST_CHECKS_DEPS) checks.o
test_log_SOURCES	= test-log.c
test_log_CFLAGS = @CHECK_CFLAGS@
test_log_LDADD = $(TEST_LOG_DEPS:%=$(top_builddir)/naemon/naemon-%) @CHECK_LIBS@ $(top_builddir)/naemon/lib/libnaemon.la -lm -ldl
check_PROGRAMS = $(TESTS)
endif
