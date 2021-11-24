#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "naemon/events.h"
#include "worker.h"
#include "lib/worker.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <string.h>
#include <stdarg.h>
#include <glib.h>

static unsigned int started, running_jobs, timeouts, reapable;
static int master_sd;
static GHashTable *ptab;

struct execution_information {
	timed_event *timed_event;
	pid_t pid;
	int state;
	struct timeval start;
	struct timeval stop;
	float runtime;
	struct rusage rusage;
};

static nm_bufferqueue *bq;

static void exit_worker(int code, const char *msg)
{
	int discard;

	if (msg) {
		perror(msg);
	}

	/*
	 * We must kill our children, so let's embark on that
	 * large scale filicide. Each process should be in a
	 * process group of its own, so we can signal not only
	 * the plugin but also all of its children.
	 */
	signal(SIGTERM, SIG_IGN);
	kill(0, SIGTERM);
	while (waitpid(-1, &discard, WNOHANG) > 0)
		; /* do nothing */
	sleep(1);
	destroy_event_queue();
	sleep(1);
	while (waitpid(-1, &discard, WNOHANG) > 0)
		; /* do nothing */

	exit(code);
}

/*
 * write a log message to master.
 * Note that this will break if we change delimiters someday,
 * but avoids doing several extra malloc()+free() for this
 * pretty simple case.
 */
__attribute__((__format__(__printf__, 1, 2)))
static void wlog(const char *fmt, ...)
{
	va_list ap;
	static char lmsg[8192] = "log=";
	int len = 4;
	size_t to_send;

	va_start(ap, fmt);
	len = vsnprintf(&lmsg[len], sizeof(lmsg) - 7, fmt, ap);
	va_end(ap);
	if (len < 0 || len + 7 >= (int)sizeof(lmsg))
		return;

	len += 4; /* log= */

	/* add delimiter and send it. 1 extra as kv pair separator */
	to_send = len + MSG_DELIM_LEN + 1;
	lmsg[len] = 0;
	memcpy(&lmsg[len + 1], MSG_DELIM, MSG_DELIM_LEN);
	if (iobroker_write_packet(nagios_iobs, master_sd, lmsg, to_send) < 0) {
		if (errno == EPIPE) {
			/* master has died or abandoned us, so exit */
			exit_worker(1, "Failed to write() to master");
		}
	}
}

static int worker_send_kvvec(int sd, struct kvvec *kvv)
{
	int ret;
	struct kvvec_buf *kvvb;

	kvvb = build_kvvec_buf(kvv);
	if (!kvvb)
		return -1;

	/* bufsize, not buflen, as it gets us the delimiter */
	ret = iobroker_write_packet(nagios_iobs, sd, kvvb->buf, (size_t)kvvb->bufsize);
	free(kvvb->buf);
	free(kvvb);

	return ret;
}

static void job_error(child_process *cp, struct kvvec *kvv, const char *fmt, ...)
{
	char msg[4096];
	int len;
	va_list ap;
	int ret;

	va_start(ap, fmt);
	len = vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
	va_end(ap);
	if (cp) {
		kvvec_addkv_str(kvv, "job_id", mkstr("%d", cp->id));
	}
	kvvec_addkv_wlen(kvv, "error_msg", 9, msg, len);
	ret = worker_send_kvvec(master_sd, kvv);
	if (ret < 0 && errno == EPIPE)
		exit_worker(1, "Failed to send job error key/value vector to master");
	kvvec_destroy(kvv, 0);
}

/* forward declaration */
static void gather_output(child_process *cp, iobuf *io, int final);

static void destroy_job(child_process *cp)
{
	running_jobs--;
	/*XXX: Maybe let this function be the value destructor for ptab? */
	g_hash_table_remove(ptab, GINT_TO_POINTER(cp->ei->pid));

	if (cp->outstd.buf) {
		free(cp->outstd.buf);
		cp->outstd.buf = NULL;
	}
	if (cp->outerr.buf) {
		free(cp->outerr.buf);
		cp->outerr.buf = NULL;
	}

	kvvec_destroy(cp->request, KVVEC_FREE_ALL);
	free(cp->cmd);
	cp->cmd = NULL;

	free(cp->ei);
	cp->ei = NULL;
	free(cp);
}

static int finish_job(child_process *cp, int reason)
{
	static struct kvvec resp = KVVEC_INITIALIZER;
	struct rusage *ru = &cp->ei->rusage;
	char *bufout, *buferr, *nul;
	int i, ret;
	size_t buflen;

	/*
	 * When a job is fininshed, the state is sent to the master, and the job
	 * should be marked with ESTALE (why just ESTALE, I don't really know, but
	 * that, and 0 is the only values that is possible)
	 */
	cp->ei->state = ESTALE;

	/* get rid of still open filedescriptors */
	if (cp->outstd.fd != -1) {
		gather_output(cp, &cp->outstd, 1);
		iobroker_close(nagios_iobs, cp->outstd.fd);
		cp->outstd.fd = -1;
	}
	if (cp->outerr.fd != -1) {
		gather_output(cp, &cp->outerr, 1);
		iobroker_close(nagios_iobs, cp->outerr.fd);
		cp->outerr.fd = -1;
	}

	/* how many key/value pairs do we need? */
	if (kvvec_init(&resp, 12 + cp->request->kv_pairs) == NULL) {
		/* what the hell do we do now? */
		exit_worker(1, "Failed to init response key/value vector");
	}

	gettimeofday(&cp->ei->stop, NULL);

	cp->ei->runtime = tv_delta_f(&cp->ei->start, &cp->ei->stop);

	/*
	 * Now build the return message.
	 * First comes the request, minus environment variables
	 */
	for (i = 0; i < cp->request->kv_pairs; i++) {
		struct key_value *kv = &cp->request->kv[i];
		/* skip environment macros */
		if (kv->key_len == 3 && !strcmp(kv->key, "env")) {
			continue;
		}
		kvvec_addkv_wlen(&resp, kv->key, kv->key_len, kv->value, kv->value_len);
	}
	kvvec_addkv_str(&resp, "wait_status", mkstr("%d", cp->ret));
	kvvec_addkv_tv(&resp, "start", &cp->ei->start);
	kvvec_addkv_tv(&resp, "stop", &cp->ei->stop);
	kvvec_addkv_str(&resp, "runtime", mkstr("%f", cp->ei->runtime));
	if (!reason) {
		/* child exited nicely (or with a signal, so check wait_status) */
		kvvec_addkv_str(&resp, "exited_ok", "1");
		kvvec_addkv_tv(&resp, "ru_utime", &ru->ru_utime);
		kvvec_addkv_tv(&resp, "ru_stime", &ru->ru_stime);
		kvvec_addkv_long(&resp, "ru_minflt", ru->ru_minflt);
		kvvec_addkv_long(&resp, "ru_majflt", ru->ru_majflt);
		kvvec_addkv_long(&resp, "ru_inblock", ru->ru_inblock);
		kvvec_addkv_long(&resp, "ru_oublock", ru->ru_oublock);
	} else {
		/* some error happened */
		kvvec_addkv_str(&resp, "exited_ok", "0");
		kvvec_addkv_str(&resp, "error_code", mkstr("%d", reason));
	}
	buflen = nm_bufferqueue_get_available(cp->outerr.buf);
	buferr = malloc(buflen);
	nm_bufferqueue_unshift(cp->outerr.buf, buflen, buferr);
	if ((nul = memchr(buferr, 0, buflen)))
		buflen = (unsigned long)nul - (unsigned long)buferr;
	kvvec_addkv_wlen(&resp, "outerr", 6, buferr, buflen);
	buflen = nm_bufferqueue_get_available(cp->outstd.buf);
	bufout = malloc(buflen);
	nm_bufferqueue_unshift(cp->outstd.buf, buflen, bufout);
	if ((nul = memchr(bufout, 0, buflen)))
		buflen = (unsigned long)nul - (unsigned long)bufout;
	kvvec_addkv_wlen(&resp, "outstd", 6, bufout, buflen);
	ret = worker_send_kvvec(master_sd, &resp);
	free(buferr);
	free(bufout);
	if (ret < 0 && errno == EPIPE)
		exit_worker(1, "Failed to send kvvec struct to master");

	return 0;
}

/*
 * Get the parent PID from a PID
 */
static int get_process_parent_id(const pid_t pid, pid_t *ppid)
{
	char buffer[BUFSIZ], *s_ppid;
	int errreading, size;
	FILE *fp;

	sprintf(buffer, "/proc/%d/stat", pid);
	fp = fopen(buffer, "r");
	if (!fp) {
		return errno;
	}

	size = fread(buffer, sizeof(char), sizeof(buffer) - 1, fp);
	if (size > 0) {
		buffer[size] = '\0';
	}
	errreading = ferror(fp);
	if (fclose(fp) != 0) {
		return errno;
	}

	if (errreading != 0) {
		return errreading;
	}

	strtok(buffer, " "); // (1) pid  %d
	strtok(NULL, " "); // (2) comm  %s
	strtok(NULL, " "); // (3) state  %c

	if ((s_ppid = strtok(NULL, " ")) == NULL) { // (4) ppid  %d
		return EBADF;
	}

	*ppid = atoi(s_ppid);
	return 0;
}

/*
 * "What can the harvest hope for, if not for the care
 * of the Reaper Man?"
 *   -- Terry Pratchett, Reaper Man
 *
 * We end up here no matter if the job is stale (ie, the child is
 * stuck in uninterruptable sleep) or if it's the first time we try
 * to kill it.
 * A job is considered reaped once we reap our direct child, in
 * which case init will become parent of our grandchildren.
 * It's also considered fully reaped if kill() results in ESRCH or
 * EPERM, or if wait()ing for the process group results in ECHILD.
 */
static void kill_job(struct nm_event_execution_properties *event)
{
	child_process *cp = event->user_data;
	int pid, id, ret, status, reaped = 0;
	pid_t ppid, wpid;

	g_return_if_fail(cp != NULL);
	g_return_if_fail(cp->ei != NULL);

	pid = cp->ei->pid;
	id = cp->id;
	if (event->execution_type == EVENT_EXEC_ABORTED) {
		(void)kill(-cp->ei->pid, SIGKILL);
		return;
	}
	/* check if the child we'r killing belongs to this worker process */
	wpid = getpid();
	status = get_process_parent_id(pid, &ppid);
	if (status != 0 || ppid != wpid) {
		/* the pid might be reallocated but still exists in child proc list */
		destroy_job(cp);
		return;
	}

	/*
	 * first attempt at reaping, so see if we just failed to
	 * notice that things were going wrong her
	 */
	if (cp->ei->state != ESTALE) {
		timeouts++;
		wlog("Killing job %d with pid %d due to timeout. timeouts=%u; started=%u", id, pid, timeouts, started);
	}

	/* brutal but efficient */
	if (kill(-cp->ei->pid, SIGKILL) < 0) {
		if (errno == ESRCH) {
			reaped = 1;
		} else {
			wlog("kill(-%d, SIGKILL) failed: %s\n", cp->ei->pid, strerror(errno));
		}
	}

	/*
	 * we must iterate at least once, in case kill() returns
	 * ESRCH when there's zombies
	 */
	do {
		ret = waitpid(cp->ei->pid, &status, WNOHANG);
		if (ret == cp->ei->pid || (ret < 0 && errno == ECHILD)) {
			reaped = 1;
		}
	} while (ret && !reaped);

	if (!ret) {
		int delay = 0;

		/*
		 * stale process (signal may not have been delivered, or
		 * the child can be stuck in uninterruptible sleep). We
		 * can't hang around forever, so just reschedule a new
		 * reap attempt later.
		 */
		if (cp->ei->state == ESTALE) {
			delay = 5;
			wlog("Failed to reap child with pid %d. Next attempt later", cp->ei->pid);
		} else {
			delay = 1;
			finish_job(cp, ETIME);
		}
		cp->ei->timed_event = schedule_event(delay,  kill_job, cp);
	} else {
		if (cp->ei->state != ESTALE)
			finish_job(cp, ETIME);
		/*
		 * Don't log if stale, since that's the normal behaviour, since we need
		 * to delay destroy_job if we finish with any dangling processes that
		 * needs to finish up. Like sendmail in a forked notification script
		 */
		destroy_job(cp);
	}
}

static void gather_output(child_process *cp, iobuf *io, int final)
{
	for (;;) {
		int rd;

		rd = nm_bufferqueue_read(io->buf, io->fd);
		if (rd < 0) {
			if (errno == EINTR) {
				/* signal caught before we read anything */
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				/* broken system or no more data. Just return */
				return;
			}
			/* Null pointer check before printing */
			if (cp && cp->ei) {
				wlog("job %d (pid=%d): Failed to read(): %s", cp->id, cp->ei->pid, strerror(errno));
			} else {
				wlog("Unknown job: Failed to read(): %s", strerror(errno));
			}
		}

		/*
		 * Close down on bad and zero read.
		 * This is the catch-all that handles EBADF, EFAULT,
		 * EINVAL and EIO, which we can't do anything about.
		 * We mustn't enter it on final reads though, as that
		 * would mean the first invocation of finish_job()
		 * would end up with a job that gets destroyed the
		 * second (or third) time its entered for the same
		 * job.
		 */
		if (rd <= 0 || final) {
			iobroker_close(nagios_iobs, io->fd);
			io->fd = -1;
			return;
		}
	}
}

static int stderr_handler(int fd, int events, void *cp_)
{
	child_process *cp = (child_process *)cp_;
	gather_output(cp, &cp->outerr, 0);
	return 0;
}

static int stdout_handler(int fd, int events, void *cp_)
{
	child_process *cp = (child_process *)cp_;
	gather_output(cp, &cp->outstd, 0);
	return 0;
}

static void sigchld_handler(int sig)
{
	reapable++;
}

static void reap_jobs(void)
{
	int reaped = 0;
	do {
		int pid, status;
		struct rusage ru;
		pid = wait3(&status, WNOHANG, &ru);
		if (pid > 0) {
			struct child_process *cp;

			if (!(cp = g_hash_table_lookup(ptab, GINT_TO_POINTER(pid)))) {
				/* we reaped a lost child. Odd that */
				continue;
			}
			reapable--;
			cp->ret = status;
			memcpy(&cp->ei->rusage, &ru, sizeof(ru));
			reaped++;
			if (cp->ei->state != ESTALE) {
				/* We leave any grandchild processes alive, until
				 * the timeout for this job has expired (at which point they
				 * will be reaped by the scheduled kill_job() event) in order
				 * to preserve compatibility with older configurations.
				 * See https://github.com/naemon/naemon-core/issues/137 for more
				 * information.
				 */
				finish_job(cp, cp->ei->state);
			}
		} else if (!pid || (pid < 0 && errno == ECHILD)) {
			reapable = 0;
		}
	} while (reapable);
}

static int start_cmd(child_process *cp)
{
	int pfd[2] = { -1, -1}, pfderr[2] = { -1, -1};

	cp->outstd.fd = runcmd_open(cp->cmd, pfd, pfderr);
	if (cp->outstd.fd < 0) {
		return -1;
	}

	cp->outerr.fd = pfderr[0];
	cp->ei->pid = runcmd_pid(cp->outstd.fd);
	/* no pid means we somehow failed */
	if (!cp->ei->pid) {
		return -1;
	}

	/* We must never block, even if plugins issue '_exit()' */
	fcntl(cp->outstd.fd, F_SETFL, O_NONBLOCK);
	fcntl(cp->outerr.fd, F_SETFL, O_NONBLOCK);
	if (iobroker_register(nagios_iobs, cp->outstd.fd, cp, stdout_handler))
		wlog("Failed to register iobroker for stdout");
	if (iobroker_register(nagios_iobs, cp->outerr.fd, cp, stderr_handler))
		wlog("Failed to register iobroker for stderr");
	g_hash_table_insert(ptab, GINT_TO_POINTER(cp->ei->pid), cp);

	return 0;
}

static child_process *parse_command_kvvec(struct kvvec *kvv)
{
	int i;
	child_process *cp;

	/* get this command's struct and insert it at the top of the list */
	cp = calloc(1, sizeof(*cp));
	if (!cp) {
		wlog("Failed to calloc() a child_process struct");
		return NULL;
	}
	cp->ei = calloc(1, sizeof(*cp->ei));
	if (!cp->ei) {
		wlog("Failed to calloc() a execution_information struct");
		return NULL;
	}

	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv = &kvv->kv[i];
		char *key = kv->key;
		char *value = kv->value;
		char *endptr;

		if (!strcmp(key, "command")) {
			cp->cmd = strdup(value);
			continue;
		}
		if (!strcmp(key, "job_id")) {
			cp->id = (unsigned int)strtoul(value, &endptr, 0);
			continue;
		}
		if (!strcmp(key, "timeout")) {
			cp->timeout = (unsigned int)strtoul(value, &endptr, 0);
			continue;
		}
	}

	/* jobs without a timeout get a default of 60 seconds. */
	if (!cp->timeout) {
		cp->timeout = 60;
	}

	return cp;
}

static void spawn_job(struct kvvec *kvv)
{
	int result;
	child_process *cp;

	if (!kvv) {
		wlog("Received NULL command key/value vector. Bug in iocache.c or kvvec.c?");
		return;
	}

	cp = parse_command_kvvec(kvv);
	if (!cp) {
		job_error(NULL, kvv, "Failed to parse worker-command");
		return;
	}
	if (!cp->cmd) {
		job_error(cp, kvv, "Failed to parse commandline. Ignoring job %u", cp->id);
		return;
	}

	gettimeofday(&cp->ei->start, NULL);
	cp->request = kvv;
	cp->ei->timed_event = schedule_event(cp->timeout, kill_job, cp);
	cp->outstd.buf = nm_bufferqueue_create();
	cp->outerr.buf = nm_bufferqueue_create();
	started++;
	running_jobs++;
	result = start_cmd(cp);
	if (result < 0) {
		job_error(cp, kvv, "Failed to start child: %s: %s", runcmd_strerror(result), strerror(errno));
		destroy_event(cp->ei->timed_event);
		running_jobs--;
	}
}

static int receive_command(int sd, int events, void *arg)
{
	int ioc_ret;
	char *buf;
	size_t size;

	if (!bq) {
		bq = nm_bufferqueue_create();
	}
	ioc_ret = nm_bufferqueue_read(bq, sd);

	if (ioc_ret == 0) {
		iobroker_close(nagios_iobs, sd);
		exit_worker(0, NULL);
	} else if (ioc_ret < 0) {
		if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
			iobroker_close(nagios_iobs, sd);
			exit_worker(0, NULL);
		}
	}

	/*
	 * loop over all inbound messages in the iocache.
	 * Since KV_TERMINATOR is a nul-byte, they're separated by 3 nuls
	 */
	while (!nm_bufferqueue_unshift_to_delim(bq, MSG_DELIM, MSG_DELIM_LEN, &size, (void **)&buf)) {
		struct kvvec *kvv;
		/* we must copy vars here, as we preserve them for the response */
		kvv = buf2kvvec(buf, (unsigned int)size - MSG_DELIM_LEN, KV_SEP, PAIR_SEP, KVVEC_COPY);
		if (kvv)
			spawn_job(kvv);
		free(buf);
	}
	return 0;
}

static void enter_worker(int sd)
{
	/* created with socketpair(), usually */
	master_sd = sd;

	ptab = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (setpgid(0, 0)) {
		/* XXX: handle error somehow, or maybe just ignore it */
	}

	/* we need to catch child signals to mark jobs as reapable */
	signal(SIGCHLD, sigchld_handler);

	fcntl(fileno(stdout), F_SETFD, FD_CLOEXEC);
	fcntl(fileno(stderr), F_SETFD, FD_CLOEXEC);
	fcntl(master_sd, F_SETFD, FD_CLOEXEC);
	nagios_iobs = iobroker_create();
	if (!nagios_iobs) {
		/* XXX: handle this a bit better */
		exit_worker(EXIT_FAILURE, "Worker failed to create io broker socket set");
	}
	init_event_queue();

	worker_set_sockopts(master_sd, 256 * 1024);

	iobroker_register(nagios_iobs, master_sd, NULL, receive_command);
	for (;;) {
		event_poll();
		reap_jobs();
	}
}

int nm_core_worker(const char *path)
{
	int sd, ret;
	char response[128];

	sd = nsock_unix(path, NSOCK_TCP | NSOCK_CONNECT);
	if (sd < 0) {
		printf("Failed to connect to query socket '%s': %s: %s\n",
		       path, nsock_strerror(sd), strerror(errno));
		return 1;
	}

	ret = nsock_printf_nul(sd, "@wproc register name=Core Worker %d;pid=%d", getpid(), getpid());
	if (ret < 0) {
		printf("Failed to register as worker.\n");
		return 1;
	}

	ret = read(sd, response, 3);
	if (ret != 3) {
		printf("Failed to read response from wproc manager\n");
		return 1;
	}
	if (memcmp(response, "OK", 3)) {
		if (read(sd, response + 3, sizeof(response) - 4) < 0) {
			printf("Failed to register with wproc manager: %s\n", strerror(errno));
		} else {
			response[sizeof(response) - 2] = 0;
			printf("Failed to register with wproc manager: %s\n", response);
		}
		return 1;
	}

	enter_worker(sd);
	return 0;
}
