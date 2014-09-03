LDADD = libnaemon.la
COV_CFLAGS = -ggdb3 -O0 -ftest-coverage -fprofile-arcs -pg
lib_LTLIBRARIES = libnaemon.la
AM_CPPFLAGS = -I$(top_builddir) -DNAEMON_COMPILATION
# details on library version numbers https://www.sourceware.org/autobook/autobook/autobook_91.html
libnaemon_la_LDFLAGS = -version-info 0:0:0
libnaemon_la_SOURCES = $(pkginclude_HEADERS) \
	bitmap.c dkhash.c fanout.c iobroker.c \
	iocache.c kvvec.c nsock.c nspath.c nsutils.c pqueue.c \
	rbtree.c runcmd.c skiplist.c snprintf.c squeue.c worker.c

check_PROGRAMS = test-bitmap test-dkhash test-fanout test-iobroker test-iocache \
	test-kvvec test-nsutils test-runcmd test-squeue

test_bitmap_SOURCES = test-bitmap.c t-utils.c t-utils.h
test_dkhash_SOURCES = test-dkhash.c t-utils.c t-utils.h
test_fanout_SOURCES = test-fanout.c t-utils.c t-utils.h
test_iobroker_SOURCES = test-iobroker.c t-utils.c t-utils.h
test_iocache_SOURCES = test-iocache.c t-utils.c t-utils.h
test_kvvec_SOURCES = test-kvvec.c t-utils.c t-utils.h
test_nsutils_SOURCES = test-nsutils.c t-utils.c t-utils.h
test_runcmd_SOURCES = test-runcmd.c t-utils.c t-utils.h
test_squeue_SOURCES = test-squeue.c t-utils.c t-utils.h

TESTS = $(check_PROGRAMS)

test: check

coverage:
	@$(MAKE) -s --no-print-directory clean
	@$(MAKE) --no-print-directory cov-build
	@$(MAKE) --no-print-directory cov-report

cov-build:
	$(MAKE) CFLAGS='$(COV_CFLAGS)' LDFLAGS='$(COV_CFLAGS)' test

cov-report:
	@for f in $(TESTS) pqueue; do gcov -b -p $$f.c >/dev/null; done
	@rm -f *.h.gcov test-*.c.gcov
	@echo "--- untested functions:" | tee -a untested
	@grep '^function .* called 0' *.c.gcov \
		| sed -e 's/function \([^ ]*\) called 0.*/\1/' \
		| sed -e 's/\.c\.gcov:/\.c:/'
