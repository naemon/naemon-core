#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include "snprintf.h"
#include "lnae-utils.h"
#include "nsock.h"

const char *nsock_strerror(int code)
{
	switch (code) {
	case NSOCK_EBIND: return "bind() failed";
	case NSOCK_ELISTEN: return "listen() failed";
	case NSOCK_ESOCKET: return "socket() failed";
	case NSOCK_EUNLINK: return "unlink() failed";
	case NSOCK_ECONNECT: return "connect() failed";
	case NSOCK_EFCNTL: return "fcntl() failed";
	case NSOCK_EINVAL: return "Invalid arguments";
	}

	return "Unknown error";
}

int nsock_unix(const char *path, unsigned int flags)
{
	struct sockaddr_un saun;
	struct sockaddr *sa;
	int sock = 0, mode;
	socklen_t slen;

	if (!path)
		return NSOCK_EINVAL;

	if (flags & NSOCK_TCP)
		mode = SOCK_STREAM;
	else if (flags & NSOCK_UDP)
		mode = SOCK_DGRAM;
	else
		return NSOCK_EINVAL;

	if ((sock = socket(AF_UNIX, mode, 0)) < 0) {
		return NSOCK_ESOCKET;
	}

	/* set up the sockaddr_un struct and the socklen_t */
	sa = (struct sockaddr *)&saun;
	memset(&saun, 0, sizeof(saun));
	saun.sun_family = AF_UNIX;
	slen = strlen(path);
	memcpy(&saun.sun_path, path, slen);
	slen += offsetof(struct sockaddr_un, sun_path);

	/* unlink if we're supposed to, but not if we're connecting */
	if (flags & NSOCK_UNLINK && !(flags & NSOCK_CONNECT)) {
		if (unlink(path) < 0 && errno != ENOENT)
			return NSOCK_EUNLINK;
	}

	if (flags & NSOCK_CONNECT) {
		if (connect(sock, sa, slen) < 0) {
			close(sock);
			return NSOCK_ECONNECT;
		}
		return sock;
	} else {
		if (bind(sock, sa, slen) < 0) {
			close(sock);
			return NSOCK_EBIND;
		}
	}

	if (!(flags & NSOCK_BLOCK) && fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
		return NSOCK_EFCNTL;

	if (flags & NSOCK_UDP)
		return sock;

	if (listen(sock, 3) < 0) {
		close(sock);
		return NSOCK_ELISTEN;
	}

	return sock;
}

static inline int nsock_vdprintf(int sd, const char *fmt, va_list ap, int plus)
{
	char *buf = NULL;
	int len, ret;

	len = vasprintf(&buf, fmt, ap);
	if (len < 0)
		return len;
	ret = write(sd, buf, len + plus);
	free(buf);
	return ret;
}

int nsock_printf_nul(int sd, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = nsock_vdprintf(sd, fmt, ap, 1);
	va_end(ap);
	return ret;
}

int nsock_printf(int sd, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = nsock_vdprintf(sd, fmt, ap, 0);
	va_end(ap);
	return ret;
}

/**
 * Write all of nbyte bytes of buf to fd, and don't let EINTR/EAGAIN stop you.
 * Returns 0 on success. On error, returns -1 and errno is set to indicate the
 * error
 */
int nsock_write_all(int fd, const void *buf, size_t nbyte)
{
	size_t c = 0;
	int ret = 0;
	while ( c < nbyte ) {
		ret = write(fd, (char *) buf + c, nbyte - c);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			return -1;
		}
		c += (size_t)ret;
	}
	return 0;
}
