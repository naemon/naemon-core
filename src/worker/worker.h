#ifndef INCLUDE_worker_h__
#define INCLUDE_worker_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <errno.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "lib/libnaemon.h"

/**
 * @file worker.h
 * @brief Naemon core worker implementation
 *
 * The default core worker accepts shell commands, executes them
 * and returns a lot of details about the execution of that
 * command. You can use worker.c as a template for writing a
 * custom worker, but using functions from it in a custom worker
 * will probably not be very useful (apart from wlog()).
 */

#ifndef ETIME
#define ETIME ETIMEDOUT
#endif

NAGIOS_BEGIN_DECL

typedef struct iobuf {
	int fd;
	nm_bufferqueue *buf;
} iobuf;

typedef struct execution_information execution_information;

typedef struct child_process {
	unsigned int id, timeout;
	char *cmd;
	int ret;
	struct kvvec *request;
	iobuf outstd;
	iobuf outerr;
	execution_information *ei;
} child_process;

/**
 * Core worker entry point
 * @param path The path to the query socket this worker should connect to
 * @return EXIT_SUCCESS or EXIT_FAILURE
 */
extern int nm_core_worker(const char *path);

NAGIOS_END_DECL

#endif
