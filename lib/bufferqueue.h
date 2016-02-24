#ifndef LIBNAEMON_bufferqueue_h__
#define LIBNAEMON_bufferqueue_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "lnae-utils.h"

NAGIOS_BEGIN_DECL

/**
 * @file bufferqueue.h
 * @brief A simple queue of buffers of binary data
 *
 * The bufferqueue library is intended for working with sockets.
 * It accepts/reads data as it becomes available, and returns/sends
 * it on demand.
 *
 * You should generally not use this directly, but instead use the iobroker
 * to push and poll data across sockets.
 *
 * @{
 */

/** opaque type for bufferqueue operations */
struct nm_bufferqueue;
typedef struct nm_bufferqueue nm_bufferqueue;

/**
 * Returns the number of unread bytes in all buffers
 *
 * @param[in] bq The bufferqueue we should look at
 * @return The number of bytes left in the bufferqueue
 */
size_t nm_bufferqueue_get_available(nm_bufferqueue *bq);

/**
 * Creates the bufferqueue object
 * @return Pointer to a valid bufferqueue object
 */
nm_bufferqueue *nm_bufferqueue_create(void);

/**
 * Destroys an bufferqueue object, freeing all memory allocated to it.
 * @param[in] bq The bufferqueue object to destroy
 */
void nm_bufferqueue_destroy(nm_bufferqueue *bq);

/**
 * Enqueue data at the end of the bufferqueue
 * The data is copied, so it can safely be taken from the stack in a
 * function that returns before the data is used.
 *
 * @param[in] bq The bufferqueue to add to
 * @param[in] buf Pointer to the data we should add
 * @param[in] len Length (in bytes) of data pointed to by buf
 * @return 0 on success, -1 on errors
 */
int nm_bufferqueue_push(nm_bufferqueue *bq, const void *buf, size_t len);

/**
 * Like nm_bufferqueue_push, except it doesn't copy all the data - instead,
 * it takes over ownership of buf.
 *
 * @param[in] bq The bufferqueue to append the data to
 * @param[in] buf The buffer we should use
 * @param[in] len The size of the buffer we should use
 * @return 0 on success, non-zero otherwise
 */
int nm_bufferqueue_push_block(nm_bufferqueue *bq, void *buf, size_t len);

/**
 * Returns a chunk of data from the front of the bufferqueue based on
 * size, but leaves the data in place for reading later.
 *
 * @param[in] bq The bufferqueue we should use data from
 * @param[in]size The size of the data we want returned
 * @param[out] buffer If not NULL and call was successful, will contain the peeked data
 * @return 0 on success, non-zero otherwise
 */
int nm_bufferqueue_peek(nm_bufferqueue *bq, size_t size, void *buffer);

/**
 * Drops a chunk of data from the front of the bufferqueue based on
 * size. This is the same as unshift, with a NULL buffer.
 *
 * @param[in] bq The bufferqueue we should use data from
 * @param[in]size The size of the data we want to drop
 * @return 0 on success, non-zero otherwise
 */
int nm_bufferqueue_drop(nm_bufferqueue *bq, size_t size);

/**
 * Dequeue a chunk of data from the front of the bufferqueue based on
 * size. If the buffer argument is not NULL, it should point to a
 * memory block large enough to hold the dequeued data. If the buffer
 * argument is NULL, the dequeued data will simply be deleted.
 * This is the same as doing peek and drop in one operation.
 *
 * @param[in] bq The bufferqueue we should use data from
 * @param[in]size The size of the data we want returned
 * @param[out] buffer If not NULL and call was successful, will contain the dequeued data
 * @return 0 on success, non-zero otherwise
 */
int nm_bufferqueue_unshift(nm_bufferqueue *bq, size_t size, void *buffer);

/**
 * Dequeue a chunk of data from the front of the bufferqueue based on
 * delimiter. If buffer is NULL, the data will simply be
 * discarded. Otherwise, *buffer will point to a newly-allocated block
 * of memory containing the dequeued data.
 *
 * The returned size and data will include the trailing delimiter.
 *
 * @param[in] bq The bufferqueue to use data from
 * @param[in] delim The delimiter
 * @param[in] delim_len Length of the delimiter
 * @param[out] size Length of the returned buffer
 * @param[out] buffer If not NULL and call was successful, will contain (caller-owned) dequeued data
 * @return 0 on success, non-zero otherwise
 */
int nm_bufferqueue_unshift_to_delim(nm_bufferqueue *bq, const char *delim, size_t delim_len, size_t *size, void **buffer);

/**
 * Read data into the bufferqueue.
 * @param[in] bq The bufferqueue we should read into
 * @param[in] fd The filedescriptor we should read from
 * @return The number of bytes read on success. < 0 on errors
 */
int nm_bufferqueue_read(nm_bufferqueue *bq, int fd);

/**
 * Like write() for buffered data
 *
 * @param[in] bq The bufferqueue to empty
 * @param[in] fd The file descriptor to send to
 * @return bytes sent on success, -ERRNO on errors
 */
int nm_bufferqueue_write(nm_bufferqueue *bq, int fd);

NAGIOS_END_DECL
#endif /* INCLUDE_bufferqueue_h__ */
/** @} */
