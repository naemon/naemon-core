#include "worker.h"
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>

struct kvvec_buf *build_kvvec_buf(struct kvvec *kvv)
{
	struct kvvec_buf *kvvb;

	/*
	 * key=value, separated by PAIR_SEP and messages
	 * delimited by MSG_DELIM
	 */
	kvvb = kvvec2buf(kvv, KV_SEP, PAIR_SEP, MSG_DELIM_LEN);
	if (!kvvb) {
		return NULL;
	}
	memcpy(kvvb->buf + (kvvb->bufsize - MSG_DELIM_LEN), MSG_DELIM, MSG_DELIM_LEN);

	return kvvb;
}

int worker_set_sockopts(int sd, int bufsize)
{
	int ret;

	ret = fcntl(sd, F_SETFD, FD_CLOEXEC);
	ret |= fcntl(sd, F_SETFL, O_NONBLOCK);

	if (!bufsize)
		return ret;
	ret |= setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int));
	ret |= setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int));

	return ret;
}

char *worker_ioc2msg(nm_bufferqueue *bq, size_t *size, int flags)
{
	char *res;
	if (nm_bufferqueue_unshift_to_delim(bq, MSG_DELIM, MSG_DELIM_LEN, size, (void **)&res))
		return NULL;
	*size -= MSG_DELIM_LEN;
	return res;
}

int spawn_named_helper(char *path, char **argv)
{
	int ret, pid;

	pid = fork();
	if (pid < 0) {
		return -1;
	}

	/* parent leaves early */
	if (pid)
		return pid;

	close_standard_fds();

	ret = execvp(path, argv);
	/* if execvp() fails, there's really nothing we can do */
	exit(ret);
}

int spawn_helper(char **argv)
{
	return spawn_named_helper(argv[0], argv);
}
