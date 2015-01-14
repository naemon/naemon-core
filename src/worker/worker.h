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
#include "lib/libnagios.h"

/**
 * @file worker.h
 * @brief Worker implementation along with various helpers
 *
 * This code isn't really in the "library" category, but it's tucked
 * in here to provide a good resource for writing remote workers and
 * as an example on how to use the API's found here.
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
 * Callback for enter_worker that simply runs a command
 */
extern int start_cmd(child_process *cp);

/**
 * To be called when a child_process has completed to ship the result to nagios
 * @param cp The child_process that describes the job
 * @param reason 0 if everything was OK, 1 if the job was unable to run
 * @return 0 on success, non-zero otherwise
 */
extern int finish_job(child_process *cp, int reason);

/**
 * Start to poll the socket and call the callback when there are new tasks
 * @param sd A socket descriptor to poll
 * @param cb The callback to call upon completion
 */
extern void enter_worker(int sd, int (*cb)(child_process*));

/**
 * Send a key/value vector as a bytestream through a socket
 * @param[in] sd The socket descriptor to send to
 * @param kvv The key/value vector to send
 * @return The number of bytes sent, or -1 on errors
 */
extern int worker_send_kvvec(int sd, struct kvvec *kvv);

NAGIOS_END_DECL

#endif
