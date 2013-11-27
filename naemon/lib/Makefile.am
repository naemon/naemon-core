test: check
noinst_LIBRARIES = libnaemon.a
AM_CPPFLAGS = -I$(top_builddir)
libnaemon_a_SOURCES = bitmap.c bitmap.h dkhash.c dkhash.h \
	fanout.c fanout.h iobroker.c iobroker.h iocache.c iocache.h \
	kvvec.c kvvec.h libnagios.h lnag-utils.h nsock.c nsock.h \
	nspath.c nspath.h nsutils.c nsutils.h pqueue.c pqueue.h \
	runcmd.c runcmd.h skiplist.c skiplist.h snprintf.c \
	snprintf.h squeue.c squeue.h worker.c worker.h

TESTS = test-bitmap test-dkhash test-fanout test-iobroker test-iocache \
	test-kvvec test-nsutils test-runcmd test-squeue

$(TESTS): libnaemon.a t-utils.c t-utils.h
