#ifndef LIBNAEMON_worker_h__
#define LIBNAEMON_worker_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lnae-utils.h"
#include "kvvec.h"
#include "bufferqueue.h"

/**
 * @file worker.h
 * @brief Worker implementation along with various helpers
 *
 * This code isn't really in the "library" category, but it's tucked
 * in here to provide a good resource for writing remote workers and
 * as an example on how to use the API's found here.
 */

NAGIOS_BEGIN_DECL

#define MSG_DELIM "\1\0\0" /**< message limiter */
#define MSG_DELIM_LEN (sizeof(MSG_DELIM)) /**< message delimiter length */
#define PAIR_SEP 0 /**< pair separator for buf2kvvec() and kvvec2buf() */
#define KV_SEP '=' /**< key/value separator for buf2kvvec() and kvvec2buf() */

/**
 * Spawn a helper with a specific process name
 * The first entry in the argv parameter will be the name of the
 * new process, unless the process changes the name itself.
 * @param path The path to the executable (can be $PATH relative)
 * @param argv Argument vector for the helper to spawn
 */
extern int spawn_named_helper(char *path, char **argv);

/**
 * Spawn any random helper process. Uses spawn_named_helper()
 * @param argv The (NULL-sentinel-terminated) argument vector
 * @return 0 on success, < 0 on errors
 */
extern int spawn_helper(char **argv);

/**
 * Build a buffer from a key/value vector buffer.
 * The resulting kvvec-buffer is suitable for sending between
 * worker and master in either direction, as it has all the
 * right delimiters in all the right places.
 * @param kvv The key/value vector to build the buffer from
 * @return NULL on errors, a newly allocated kvvec buffer on success
 */
extern struct kvvec_buf *build_kvvec_buf(struct kvvec *kvv);

/**
 * Grab a worker message from an iocache buffer
 * @param[in] ioc The io cache
 * @param[out] size Out buffer for buffer length
 * @param[in] flags Currently unused
 * @return A buffer from the iocache on succes; NULL on errors
 */
extern char *worker_ioc2msg(nm_bufferqueue *ioc, size_t *size, int flags);

/**
 * Set some common socket options
 * @param[in] sd The socket to set options for
 * @param[in] bufsize Size to set send and receive buffers to
 * @return 0 on success. < 0 on errors
 */
extern int worker_set_sockopts(int sd, int bufsize);

NAGIOS_END_DECL

#endif
