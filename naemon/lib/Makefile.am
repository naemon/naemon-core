test: check
lib_LTLIBRARIES = libnaemon.la
AM_CPPFLAGS = -I$(top_builddir)
pkginclude_HEADERS = libnaemon.h libnagios.h bitmap.h dkhash.h \
	fanout.h iobroker.h iocache.h kvvec.h lnag-utils.h \
	nsock.h nspath.h nsutils.h pqueue.h runcmd.h skiplist.h \
	snprintf.h squeue.h worker.h
# details on library version numbers https://www.sourceware.org/autobook/autobook/autobook_91.html
libnaemon_la_LDFLAGS = -version-info 0:0:0
libnaemon_la_SOURCES = $(pkginclude_HEADERS) \
	bitmap.c dkhash.c fanout.c iobroker.c \
	iocache.c kvvec.c nsock.c nspath.c nsutils.c pqueue.c \
	runcmd.c skiplist.c snprintf.c squeue.c worker.c

TESTS = test-bitmap test-dkhash test-fanout test-iobroker test-iocache \
	test-kvvec test-nsutils test-runcmd test-squeue

$(TESTS): .libs/libnaemon.a t-utils.c t-utils.h

distclean-local:
	rm -rf Makefile.in
