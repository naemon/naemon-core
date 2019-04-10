#include "bufferqueue.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

/**
 * The struct that represents a single buffer in the nm_bufferqueue.
 */
struct bufferqueue_buffer {
	char *bqb_buf; /* the data */
	unsigned long bqb_offset; /* where we're reading in the buffer */
	unsigned long bqb_bufsize; /* the amount of data in this buffer in total */
	struct bufferqueue_buffer *bqb_next; /* link to the next buffer after this */
};

/**
 * The struct that represents a whole bufferqueue. It contains bufferqueue_buffer
 */
struct nm_bufferqueue {
	struct bufferqueue_buffer *bq_front; /**< A pointer to the head of the queue. */
	struct bufferqueue_buffer *bq_back; /**< A pointer to the tail of the queue. */
	size_t bq_available; /**< The number of bytes in this bq. */
};

static struct bufferqueue_buffer *bufferqueue_buffer_create(void)
{
	return calloc(1, sizeof(struct bufferqueue_buffer));
}

static void bufferqueue_buffer_destroy(struct bufferqueue_buffer *buffer)
{
	free(buffer->bqb_buf);
	buffer->bqb_buf = NULL;
	free(buffer);
}

nm_bufferqueue *nm_bufferqueue_create()
{
	nm_bufferqueue *bq;
	bq = calloc(1, sizeof(*bq));
	return bq;
}

void nm_bufferqueue_destroy(nm_bufferqueue *bq)
{
	if (!bq)
		return;

	while (bq->bq_front) {
		struct bufferqueue_buffer *buffer = bq->bq_front->bqb_next;
		bufferqueue_buffer_destroy(bq->bq_front);
		bq->bq_front = buffer;
	}
	free(bq);
}

size_t nm_bufferqueue_get_available(nm_bufferqueue *bq)
{
	if (!bq)
		return 0;

	return bq->bq_available;
}

int nm_bufferqueue_peek(nm_bufferqueue *bq, size_t size, void *buffer)
{
	size_t used = 0; /* bytes used across all buffers - counts up towards initial size as size shrinks to 0 */
	size_t left_in_buffer = 0; /* size left per buffer - after the first, this should be bytes in buffer */
	struct bufferqueue_buffer *current_buf;
	if (!bq || bq->bq_available < size)
		return -1;
	if (!size)
		return 0;
	current_buf = bq->bq_front;

	while ((left_in_buffer = current_buf->bqb_bufsize - current_buf->bqb_offset) <= size) {
		if (buffer)
			memcpy(
				buffer + used,
				current_buf->bqb_buf + current_buf->bqb_offset,
				left_in_buffer);
		current_buf = current_buf->bqb_next;
		size -= left_in_buffer;
		used += left_in_buffer;
		if (!current_buf) {
			if (!size)
				return 0;
			else
				return -1;
		}
	}

	if (size) {
		if (buffer)
			memcpy(
				buffer + used,
				current_buf->bqb_buf + current_buf->bqb_offset,
				size);
	}

	return 0;
}

int nm_bufferqueue_drop(nm_bufferqueue *bq, size_t size)
{
	size_t left_in_buffer = 0;
	if (!bq || bq->bq_available < size)
		return -1;
	if (!size)
		return 0;
	while ((left_in_buffer = bq->bq_front->bqb_bufsize - bq->bq_front->bqb_offset) <= size) {
		struct bufferqueue_buffer *new_head = bq->bq_front->bqb_next;
		size -= left_in_buffer;
		bufferqueue_buffer_destroy(bq->bq_front);
		bq->bq_front = new_head;
		bq->bq_available -= left_in_buffer;
		if (!bq->bq_front) {
			bq->bq_back = NULL;
			if (!size)
				return 0;
			else
				return -1;
		}
	}

	if (size) {
		bq->bq_front->bqb_offset += size;
		bq->bq_available -= size;
	}
	return 0;
}

int nm_bufferqueue_unshift(nm_bufferqueue *bq, size_t size, void *buffer)
{
	int was_error = 0;
	if (buffer)
		was_error = nm_bufferqueue_peek(bq, size, buffer);
	if (!was_error)
		return nm_bufferqueue_drop(bq, size);
	return was_error;
}

static int is_delim_match(struct bufferqueue_buffer *buffer, const char *position, const char *delim, size_t delim_len)
{
	size_t matches;
	for (matches = 0; matches < delim_len; matches++) {
		if ((unsigned long)position + (unsigned long)matches >= (unsigned long)buffer->bqb_buf + (unsigned long)buffer->bqb_bufsize) {
			if (buffer->bqb_next == NULL)
				return 0;
			buffer = buffer->bqb_next;
			position = buffer->bqb_buf - matches;
		}
		if (position[matches] != delim[matches]) {
			return 0;
		}
	}
	return 1;
}

int nm_bufferqueue_unshift_to_delim(nm_bufferqueue *bq, const char *delim, size_t delim_len, size_t *size, void **buffer)
{
	struct bufferqueue_buffer *bqbuffer;

	if (!buffer)
		return -1;
	else
		*buffer = NULL;

	if (!bq || !bq->bq_front)
		return -1;

	*size = 0;

	bqbuffer = bq->bq_front;

	while (bqbuffer) {
		char *ptr = NULL;
		size_t remains = bqbuffer->bqb_bufsize - bqbuffer->bqb_offset;
		char *buf = bqbuffer->bqb_buf + bqbuffer->bqb_offset;
		while (remains) {
			unsigned long jump;
			size_t ioc_start;

			ptr = memchr(buf, *delim, remains);
			if (!ptr) {
				break;
			}
			if (!is_delim_match(bqbuffer, ptr, delim, delim_len)) {
				jump = 1 + (unsigned long)ptr - (unsigned long)buf;
				remains -= jump;
				buf += jump;
				continue;
			}
			/* it was a delimiter match, so return it */

			ioc_start = (size_t)bqbuffer->bqb_buf + bqbuffer->bqb_offset;
			*size += (size_t)ptr - ioc_start;
			*size += delim_len;

			if ((*buffer = calloc(*size, 1)) == NULL )
				return -1;

			if (nm_bufferqueue_unshift(bq, *size, *buffer)) {
				/* we fucked up */
				free(*buffer);
				*buffer = NULL;
				return -1;
			}
			return 0;
		}
		*size += bqbuffer->bqb_bufsize - bqbuffer->bqb_offset;
		bqbuffer = bqbuffer->bqb_next;
	}
	return -1;
}

int nm_bufferqueue_read(nm_bufferqueue *bq, int fd)
{
	char *buffer;
	int avail = 0;

	if (ioctl(fd, FIONREAD, &avail) < 0) {
		return -1;
	}

	if (avail == 0) {
		/* EOF or EAGAIN? With a bit of luck, read() will fail and tell us.
		 * Without luck, it was EAGAIN, but now it isn't,
		 * so we have to deal with that, too...
		 */
		char failbuf[128];
		avail = read(fd, failbuf, 128);
		if (avail > 0)
			nm_bufferqueue_push(bq, failbuf, avail);
		return avail;
	}

	if ((buffer = malloc(avail)) == NULL) {
		errno = -ENOMEM;
		return -1;
	}

	if (read(fd, buffer, avail) < 0) {
		return -1;
	}

	if (nm_bufferqueue_push_block(bq, buffer, avail)) {
		errno = -ENOMEM;
		free(buffer);
		return -1;
	}
	return avail;
}

int nm_bufferqueue_push_block(nm_bufferqueue *bq, void *buf, size_t len)
{
	struct bufferqueue_buffer *buffer;
	if (len == 0)
		return 0;
	buffer = bufferqueue_buffer_create();
	buffer->bqb_buf = buf;
	buffer->bqb_bufsize = len;
	if (!bq->bq_front) {
		bq->bq_front = bq->bq_back = buffer;
	} else {
		bq->bq_back->bqb_next = buffer;
		bq->bq_back = buffer;
	}
	bq->bq_available += len;
	return 0;
}

int nm_bufferqueue_push(nm_bufferqueue *bq, const void *buf, size_t len)
{
	char *internal_buf;
	if (!bq)
		return -1;
	if (len == 0)
		return 0;

	internal_buf = calloc(len, 1);
	memcpy(internal_buf, buf, len);
	return nm_bufferqueue_push_block(bq, internal_buf, len);
}

int nm_bufferqueue_write(nm_bufferqueue *bq, int fd)
{
	unsigned int sent = 0;

	errno = 0;
	if (!bq)
		return -1;

	if (!bq->bq_front)
		return 0;

	if (fd < 0)
		return -1;

	while (bq->bq_front) {
		char *buf = bq->bq_front->bqb_buf + bq->bq_front->bqb_offset;
		size_t len = bq->bq_front->bqb_bufsize - bq->bq_front->bqb_offset;

		int new_sent = write(fd, buf, len);
		if (new_sent < 0) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
				/* will be retried when the next job is executed, so don't
				 * return error */
				return sent;
			}
			return -errno;
		}
		sent += new_sent;

		nm_bufferqueue_unshift(bq, new_sent, NULL);
	}

	return sent;
}
