#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "iobroker.h"
#include "bufferqueue.h"

/*
 * epoll_*() is linux specific and was added to glibc 2.3.2, so we
 * check for 2.4 and use epoll() if we're on that version or later.
 */
#if defined(__GLIBC__) && defined(__linux)
#include <features.h>
# if __GLIBC_PREREQ(2, 4) && !defined(IOBROKER_USES_SELECT) && !defined(IOBROKER_USES_POLL)
#  define IOBROKER_USES_EPOLL
# endif
#endif

#ifdef IOBROKER_USES_EPOLL
#include <sys/epoll.h>
/* these were added later */
#ifndef EPOLLRDHUP
# define EPOLLRDHUP 0
#endif
#ifndef EPOLLONESHOT
# define EPOLLONESHOT 0
#endif
#elif !defined(IOBROKER_USES_SELECT)
#include <poll.h>
#else
#include <sys/select.h>
#endif

#if defined(IOBROKER_USES_EPOLL) && defined(IOBROKER_USES_POLL)
# error "iobroker can't use both epoll() and poll()"
#elif defined(IOBROKER_USES_EPOLL) && defined(IOBROKER_USES_SELECT)
# error "iobroker can't use both epoll() and select()"
#elif defined(IOBROKER_USEES_POLL) && defined(IOBROKER_USES_SELECT)
# error "iobroker can't use both poll() and select()"
#endif

typedef struct {
	int fd; /* the file descriptor */
	int events; /* events the caller is interested in */
	int (*handler)(int, int, void *); /* where we send data */
	void *arg; /* the argument we send to the input handler */
	nm_bufferqueue *bq_out;
} iobroker_fd;


struct iobroker_set {
	iobroker_fd **iobroker_fds;
	int max_fds; /* max number of sockets we can accept */
	int num_fds; /* number of sockets we're currently brokering for */
#ifdef IOBROKER_USES_EPOLL
	int epfd;
	struct epoll_event *ep_events;
#elif !defined(IOBROKER_USES_SELECT)
	struct pollfd *pfd;
#endif
};

static struct {
	int code;
	const char *string;
} iobroker_errors[] = {
	{ IOBROKER_SUCCESS, "Success" },
	{ IOBROKER_ENOSET, "IOB set is NULL" },
	{ IOBROKER_ENOINIT, "IOB set not initialized" },
};
static const char *iobroker_unknown_error = "unknown error";

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif
const char *iobroker_strerror(int error)
{
	if (error == IOBROKER_ELIB)
		return strerror(errno);
	error = (~error) + 1;
	if (error < 0) {
		return iobroker_unknown_error;
	}
	if (error >= (int)ARRAY_SIZE(iobroker_errors))
		return strerror(error);

	return iobroker_errors[error].string;
}


int iobroker_max_usable_fds(void)
{
#if defined(RLIMIT_NOFILE)
	struct rlimit rlim;
	getrlimit(RLIMIT_NOFILE, &rlim);
	return (unsigned long)rlim.rlim_cur;
#elif defined(_SC_OPEN_MAX)
	return (unsigned long)sysconf(_SC_OPEN_MAX);
#elif defined(OPEN_MAX)
	return (unsigned long)OPEN_MAX;
#elif defined(_POSIX_OPEN_MAX)
	return (unsigned long)_POSIX_OPEN_MAX;
#else
	/*
	 * No sysconf macros, no rlimit and no hopefully-sane
	 * defaults so we just guess. This might be completely
	 * wrong and could cause segfaults
	 */
	return 256UL;
#endif
}


int iobroker_get_max_fds(iobroker_set *iobs)
{
	if (!iobs)
		return IOBROKER_ENOSET;
	return iobs->max_fds;
}

int iobroker_get_num_fds(iobroker_set *iobs)
{
	if (!iobs)
		return IOBROKER_ENOSET;
	return iobs->num_fds;
}

struct iobroker_set *iobroker_create(void)
{
	iobroker_set *iobs = NULL;

	iobs = calloc(1, sizeof(*iobs));
	if (!iobs) {
		goto error_out;
	}

	iobs->max_fds = iobroker_max_usable_fds();
	/* add sane max limit, if ulimit is set to unlimited or a very high value we
	 * don't want to waste memory for nothing */
	if (iobs->max_fds > MAX_FD_LIMIT)
		iobs->max_fds = MAX_FD_LIMIT;
	iobs->iobroker_fds = calloc(iobs->max_fds, sizeof(iobroker_fd *));
	if (!iobs->iobroker_fds) {
		goto error_out;
	}

#ifdef IOBROKER_USES_EPOLL
	{
		int flags;

		iobs->ep_events = calloc(iobs->max_fds, sizeof(struct epoll_event));
		if (!iobs->ep_events) {
			goto error_out;
		}

		iobs->epfd = epoll_create(iobs->max_fds);
		if (iobs->epfd < 0) {
			goto error_out;
		}

		flags = fcntl(iobs->epfd, F_GETFD);
		flags |= FD_CLOEXEC;
		fcntl(iobs->epfd, F_SETFD, flags);
	}
#elif !defined(IOBROKER_USES_SELECT)
	iobs->pfd = calloc(iobs->max_fds, sizeof(struct pollfd));
	if (!iobs->pfd)
		goto error_out;
#endif

	return iobs;

error_out:
	if (iobs) {
#ifdef IOBROKER_USES_EPOLL
		close(iobs->epfd);
		if (iobs->ep_events)
			free(iobs->ep_events);
#endif
		if (iobs->iobroker_fds)
			free(iobs->iobroker_fds);
		free(iobs);
	}
	return NULL;
}

static int reg_one(iobroker_set *iobs, int fd, int events, void *arg, int (*handler)(int, int, void *))
{
	iobroker_fd *s;

	if (!iobs) {
		return IOBROKER_ENOSET;
	}
	if (fd < 0 || fd > iobs->max_fds)
		return IOBROKER_EINVAL;

	/*
	 * Re-registering a socket is an error, as multiple input
	 * handlers for a single socket makes no sense at all
	 */
	if (iobs->iobroker_fds[fd] != NULL)
		return IOBROKER_EALREADY;

#ifdef IOBROKER_USES_EPOLL
	{
		struct epoll_event ev;
		ev.events = events;
		ev.data.ptr = arg;
		ev.data.fd = fd;
		if (epoll_ctl(iobs->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
			return IOBROKER_ELIB;
		}
	}
#endif

	s = calloc(1, sizeof(iobroker_fd));
	s->handler = handler;
	s->fd = fd;
	s->arg = arg;
	s->events = events;
	s->bq_out = nm_bufferqueue_create();
	iobs->iobroker_fds[fd] = s;
	iobs->num_fds++;

	return 0;
}

int iobroker_register(iobroker_set *iobs, int fd, void *arg, int (*handler)(int, int, void *))
{
#ifdef IOBROKER_USES_EPOLL
	return reg_one(iobs, fd, EPOLLIN | EPOLLRDHUP, arg, handler);
#else
	return reg_one(iobs, fd, POLLIN, arg, handler);
#endif
}

int iobroker_register_out(iobroker_set *iobs, int fd, void *arg, int (*handler)(int, int, void *))
{
#ifdef IOBROKER_USES_EPOLL
	return reg_one(iobs, fd, EPOLLOUT, arg, handler);
#else
	return reg_one(iobs, fd, POLLOUT, arg, handler);
#endif
}

int iobroker_is_registered(iobroker_set *iobs, int fd)
{
	if (!iobs || fd < 0 || fd > iobs->max_fds || !iobs->iobroker_fds[fd])
		return 0;

	return iobs->iobroker_fds[fd]->fd == fd;
}

int iobroker_unregister(iobroker_set *iobs, int fd)
{
	if (!iobs)
		return IOBROKER_ENOSET;

	if (!iobs->iobroker_fds)
		return IOBROKER_ENOINIT;

	if (fd < 0 || fd >= iobs->max_fds || !iobs->iobroker_fds[fd])
		return IOBROKER_EINVAL;

	nm_bufferqueue_destroy(iobs->iobroker_fds[fd]->bq_out);
	iobs->iobroker_fds[fd]->bq_out = NULL;

	free(iobs->iobroker_fds[fd]);
	iobs->iobroker_fds[fd] = NULL;
	if (iobs->num_fds > 0)
		iobs->num_fds--;

#ifdef IOBROKER_USES_EPOLL
	{
		/*
		 * This needs to be set for linux <= 2.6.9 even though
		 * it's ignored even then.
		 */
		struct epoll_event ev;

		return epoll_ctl(iobs->epfd, EPOLL_CTL_DEL, fd, &ev);
	}
#endif
	return 0;
}


int iobroker_deregister(iobroker_set *iobs, int fd)
{
	return iobroker_unregister(iobs, fd);
}


int iobroker_close(iobroker_set *iobs, int fd)
{
	int result;

	result = iobroker_unregister(iobs, fd);
	if (fd >= 0) /* make sure valgrind shuts up */
		(void)close(fd);
	return result;
}


void iobroker_destroy(iobroker_set *iobs, int flags)
{
	int i;
	int (*dereg)(iobroker_set *, int) = iobroker_unregister;

	if (!iobs)
		return;

	if (flags & IOBROKER_CLOSE_SOCKETS) {
		dereg = iobroker_close;
	}

#ifdef IOBROKER_USES_EPOLL
	if (iobs->epfd >= 0)
		close(iobs->epfd);
#elif !defined(IOBROKER_USES_SELECT)
	if (iobs->pfd)
		free(iobs->pfd);
#endif

	if (!iobs->iobroker_fds)
		return;

	for (i = 0; i < iobs->max_fds; i++) {
		if (iobs->iobroker_fds[i] == NULL)
			continue;
		dereg(iobs, i);
	}
	free(iobs->iobroker_fds);
	iobs->iobroker_fds = NULL;
#ifdef IOBROKER_USES_EPOLL
	free(iobs->ep_events);
	close(iobs->epfd);
#endif
	free(iobs);
}


int iobroker_poll(iobroker_set *iobs, int timeout)
{
	int i, nfds, ret = 0;

	if (!iobs)
		return IOBROKER_ENOSET;

#if defined(IOBROKER_USES_EPOLL)
	nfds = epoll_wait(iobs->epfd, iobs->ep_events,
	                  /* to gain consistent "idling" behaviour with the other mechanisms,
	                   * we avoid returning immediately here by "faking" maxevents */
	                  !iobs->num_fds ? 1 : iobs->num_fds,
	                  timeout);
	if (nfds < 0) {
		return IOBROKER_ELIB;
	}

	for (i = 0; i < nfds; i++) {
		int fd;
		iobroker_fd *s = NULL;

		fd = iobs->ep_events[i].data.fd;
		if (fd < 0 || fd > iobs->max_fds) {
			continue;
		}
		s = iobs->iobroker_fds[fd];

		if (s) {
			s->handler(fd, iobs->ep_events[i].events, s->arg);
			ret++;
		}
	}
#elif defined(IOBROKER_USES_SELECT)
	/*
	 * select() is the (last) fallback, as it's the least
	 * efficient by quite a huge margin, so it has to be
	 * specified specially (in CFLAGS) and should only be
	 * used if epoll() or poll() doesn't work properly.
	 */
	{
		fd_set read_fds;
		int num_fds = 0;
		struct timeval tv;

		FD_ZERO(&read_fds);
		for (i = 0; i < iobs->max_fds; i++) {
			if (!iobs->iobroker_fds[i])
				continue;
			num_fds++;
			FD_SET(iobs->iobroker_fds[i]->fd, &read_fds);
			if (num_fds == iobs->num_fds)
				break;
		}
		if (timeout >= 0) {
			tv.tv_sec = timeout / 1000;
			tv.tv_usec = (timeout % 1000) * 1000;
			nfds = select(iobs->max_fds, &read_fds, NULL, NULL, &tv);
		} else { /* timeout of -1 means poll indefinitely */
			nfds = select(iobs->max_fds, &read_fds, NULL, NULL, NULL);
		}
		if (nfds < 0) {
			return IOBROKER_ELIB;
		}
		num_fds = 0;
		for (i = 0; i < iobs->max_fds; i++) {
			if (!iobs->iobroker_fds[i])
				continue;
			if (FD_ISSET(iobs->iobroker_fds[i]->fd, &read_fds)) {
				iobroker_fd *s = iobs->iobroker_fds[i];
				if (!s) {
					/* this should be logged somehow */
					continue;
				}
				s->handler(s->fd, POLLIN, s->arg);
				ret++;
			}
		}
	}
#else
	/*
	 * poll(2) is an acceptable fallback if level-triggered epoll()
	 * isn't available.
	 */
	{
		int p = 0;

		for (i = 0; i < iobs->max_fds; i++) {
			if (!iobs->iobroker_fds[i])
				continue;
			iobs->pfd[p].fd = iobs->iobroker_fds[i]->fd;
			iobs->pfd[p].events = POLLIN;
			p++;
		}
		nfds = poll(iobs->pfd, iobs->num_fds, timeout);
		if (nfds < 0) {
			return IOBROKER_ELIB;
		}
		for (i = 0; i < iobs->num_fds; i++) {
			iobroker_fd *s;
			if ((iobs->pfd[i].revents & POLLIN) != POLLIN) {
				continue;
			}

			s = iobs->iobroker_fds[iobs->pfd[i].fd];
			if (!s) {
				/* this should be logged somehow */
				continue;
			}
			s->handler(s->fd, (int)iobs->pfd[i].revents, s->arg);
			ret++;
		}
	}
#endif

	return ret;
}

int iobroker_push(iobroker_set *iobs)
{
	int i, result = 1;

	if (!iobs)
		return 1;

	for (i = 0; i < iobs->max_fds; i++) {
		iobroker_fd *s = NULL;

		if (!iobs->iobroker_fds[i])
			continue;

		s = iobs->iobroker_fds[i];
		if (s->fd > 0 && nm_bufferqueue_get_available(s->bq_out)) {
			int ret;
			result = 0;
			ret = nm_bufferqueue_write(s->bq_out, s->fd);
			if (ret < 0) {
				/* TODO: can't log() in lib */
			}
		}
	}
	return result;
}

int iobroker_write_packet(iobroker_set *iobs, int fd, char *buf, size_t len)
{
	int ret = 0;
	if ((ret = nm_bufferqueue_push(iobs->iobroker_fds[fd]->bq_out, buf, len)))
		return ret;
	/* horrible idea? */
	return iobroker_push(iobs);
}
