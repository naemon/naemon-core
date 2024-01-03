/*
 * This file holds all naemon<->libnaemon integration stuff, so that
 * libnaemon itself is usable as a standalone library for addon
 * writers to use as they see fit.
 *
 * This means apis inside libnaemon can be tested without compiling
 * all of Naemon into it, and that they can remain general-purpose
 * code that can be reused for other things later.
 */
#include "workers.h"
#include "config.h"
#include <string.h>
#include "query-handler.h"
#include "utils.h"
#include "logging.h"
#include "globals.h"
#include "defaults.h"
#include "nm_alloc.h"
#include "events.h"
#include "lib/worker.h"
#include <sys/types.h>
#include <sys/wait.h>

/* perfect hash function for wproc response codes */
#include "wpres-phash.h"
#include <glib.h>

struct wproc_worker;

struct wproc_job {
	unsigned int id;
	unsigned int timeout;
	char *command;
	void (*callback)(struct wproc_result *, void *, int);
	void *data;
	struct wproc_worker *wp;
};

struct wproc_list;

struct wproc_worker {
	char *name; /**< check-source name of this worker */
	int sd;     /**< communication socket */
	pid_t pid;  /**< pid */
	int max_jobs; /**< Max number of jobs the worker can handle */
	int jobs_started; /**< jobs started */
	int job_index; /**< round-robin slot allocator (this wraps) */
	nm_bufferqueue *bq;  /**< bufferqueue for reading from worker */
	GHashTable *jobs; /**< array of jobs */
	struct wproc_list *wp_list;
};

struct wproc_list {
	unsigned int len;
	unsigned int idx;
	struct wproc_worker **wps;
};

static struct wproc_list workers = {0, 0, NULL};

static GHashTable *specialized_workers;
static struct wproc_list *to_remove = NULL;

unsigned int wproc_num_workers_online = 0, wproc_num_workers_desired = 0;
unsigned int wproc_num_workers_spawned = 0;

static int get_desired_workers(int desired_workers);
static int spawn_core_worker(void);

#define tv2float(tv) ((float)((tv)->tv_sec) + ((float)(tv)->tv_usec) / 1000000.0)

static void wproc_logdump_buffer(int debuglevel, int verbosity, const char *prefix, char *buf)
{
	char *ptr, *eol;
	unsigned int line = 1;

	if (!buf || !*buf)
		return;
	for (ptr = buf; ptr && *ptr; ptr = eol ? eol + 1 : NULL) {
		if ((eol = strchr(ptr, '\n')))
			* eol = 0;
		log_debug_info(debuglevel, verbosity, "%s line %.02d: %s\n", prefix, line++, ptr);
		if (eol)
			*eol = '\n';
		else
			break;
	}
}

static int get_job_id(struct wproc_worker *wp)
{
	return wp->job_index++;
}

static struct wproc_job *get_job(struct wproc_worker *wp, int job_id)
{
	return g_hash_table_lookup(wp->jobs, GINT_TO_POINTER(job_id));
}


static struct wproc_list *get_wproc_list(const char *cmd)
{
	struct wproc_list *wp_list;
	char *cmd_name = NULL, *slash = NULL, *space;

	if (G_UNLIKELY(specialized_workers == NULL))
		return &workers;

	if (!g_hash_table_size(specialized_workers))
		return &workers;

	/* first, look for a specialized worker for this command */
	if ((space = strchr(cmd, ' ')) != NULL) {
		int namelen = (unsigned long)space - (unsigned long)cmd;
		cmd_name = nm_calloc(1, namelen + 1);
		memcpy(cmd_name, cmd, namelen);
		slash = strrchr(cmd_name, '/');
	}

	wp_list = g_hash_table_lookup(specialized_workers, cmd_name ? cmd_name : cmd);
	if (!wp_list && slash) {
		wp_list = g_hash_table_lookup(specialized_workers, ++slash);
	}
	if (wp_list != NULL) {
		log_debug_info(DEBUGL_CHECKS, 1, "Found specialized worker(s) for '%s'\n", (slash && *slash != '/') ? slash : (cmd_name ? cmd_name : "(null)"));
	}
	if (cmd_name)
		free(cmd_name);

	return wp_list ? wp_list : &workers;
}

static struct wproc_worker *get_worker(const char *cmd)
{
	struct wproc_list *wp_list;
	struct wproc_worker *worker = NULL;
	size_t i, boundary;

	if (!cmd)
		return NULL;

	wp_list = get_wproc_list(cmd);
	if (!wp_list || !wp_list->wps || !wp_list->len)
		return NULL;

	/* Try to find a worker that is not overloaded. We go one lap around the
	 * list before giving up. */
	i = boundary = wp_list->idx % wp_list->len;
	do {
		i = (i + 1) % wp_list->len;
		if (g_hash_table_size(wp_list->wps[i]->jobs) < (unsigned int) wp_list->wps[i]->max_jobs) {
			/* We found one! */
			wp_list->idx = i;
			worker = wp_list->wps[i];
			break;
		}
	} while (i != boundary);

	return worker;
}

static void run_job_callback(struct wproc_job *job, struct wproc_result *wpres, int val)
{
	if (!job || !job->callback)
		return;
	
	if (!wpres) {
		return;
	}

	(*job->callback)(wpres, job->data, val);
	job->callback = NULL;
}

static void destroy_job(gpointer job_)
{
	struct wproc_job *job = job_;
	if (!job)
		return;

	/* call with NULL result to make callback clean things up */
	run_job_callback(job, NULL, 0);

	nm_free(job->command);
	free(job);
}

static int wproc_is_alive(struct wproc_worker *wp)
{
	if (!wp || !wp->pid)
		return 0;
	if (kill(wp->pid, 0) == 0 && iobroker_is_registered(nagios_iobs, wp->sd))
		return 1;
	return 0;
}

static int wproc_destroy(struct wproc_worker *wp, int flags)
{
	int i = 0, force = 0, self;

	if (!wp)
		return 0;

	force = !!(flags & WPROC_FORCE);

	self = getpid();

	if (self == nagios_pid && !force)
		return 0;

	/* free all memory when either forcing or a worker called us */
	nm_bufferqueue_destroy(wp->bq);
	wp->bq = NULL;
	nm_free(wp->name);
	g_hash_table_destroy(wp->jobs);
	wp->jobs = NULL;

	/* workers must never control other workers, so they return early */
	if (self != nagios_pid)
		return 0;

	/* kill(0, SIGKILL) equals suicide, so we avoid it */
	if (wp->pid) {
		kill(wp->pid, SIGKILL);
	}

	iobroker_close(nagios_iobs, wp->sd);

	/* reap this child if it still exists */
	do {
		int ret = waitpid(wp->pid, &i, 0);
		if (ret == wp->pid || (ret < 0 && errno == ECHILD))
			break;
	} while (1);

	free(wp);

	return 0;
}

/* remove the worker list pointed to by to_remove */
static gboolean remove_specialized(gpointer key, gpointer value, gpointer data)
{
	return value == data;
}

/* remove worker from job assignment list */
static void remove_worker(struct wproc_worker *worker)
{
	unsigned int i, j = 0;
	struct wproc_list *wpl = worker->wp_list;
	for (i = 0; i < wpl->len; i++) {
		if (wpl->wps[i] == worker)
			continue;
		wpl->wps[j++] = wpl->wps[i];
	}
	wpl->len = j;

	if (!specialized_workers || wpl->len)
		return;

	to_remove = wpl;
	g_hash_table_foreach_remove(specialized_workers, remove_specialized, to_remove);
}


/*
 * This gets called from both parent and worker process, so
 * we must take care not to blindly shut down everything here
 */
void free_worker_memory(int flags)
{
	if (workers.wps) {
		unsigned int i;

		for (i = 0; i < workers.len; i++) {
			if (!workers.wps[i])
				continue;

			wproc_destroy(workers.wps[i], flags);
			workers.wps[i] = NULL;
		}

		free(workers.wps);
	}
	g_hash_table_foreach_remove(specialized_workers, remove_specialized, NULL);
	g_hash_table_destroy(specialized_workers);
	workers.wps = NULL;
	workers.len = 0;
	workers.idx = 0;
}

static int str2timeval(char *str, struct timeval *tv)
{
	char *ptr, *ptr2;

	tv->tv_sec = strtoul(str, &ptr, 10);
	if (ptr == str) {
		tv->tv_sec = tv->tv_usec = 0;
		return -1;
	}
	if (*ptr == '.' || *ptr == ',') {
		ptr2 = ptr + 1;
		tv->tv_usec = strtoul(ptr2, &ptr, 10);
	}
	return 0;
}

/*
 * parses a worker result. We do no strdup()'s here, so when
 * kvv is destroyed, all references to strings will become
 * invalid
 */
static int parse_worker_result(wproc_result *wpres, struct kvvec *kvv)
{
	int i;

	for (i = 0; i < kvv->kv_pairs; i++) {
		struct wpres_key *k;
		char *key, *value;
		key = kvv->kv[i].key;
		value = kvv->kv[i].value;

		k = wpres_get_key(key, kvv->kv[i].key_len);
		if (!k) {
			nm_log(NSLOG_RUNTIME_WARNING, "wproc: Unrecognized result variable: (i=%d) %s=%s\n", i, key, value);
			continue;
		}
		switch (k->code) {
		case WPRES_job_id:
			wpres->job_id = atoi(value);
			break;
		case WPRES_command:
			wpres->command = value;
			break;
		case WPRES_timeout:
			wpres->timeout = atoi(value);
			break;
		case WPRES_wait_status:
			wpres->wait_status = atoi(value);
			break;
		case WPRES_start:
			str2timeval(value, &wpres->start);
			break;
		case WPRES_stop:
			str2timeval(value, &wpres->stop);
			break;
		case WPRES_type:
			/* Keep for backward compatibility of nagios special purpose workers */
			break;
		case WPRES_outstd:
			wpres->outstd = value;
			break;
		case WPRES_outerr:
			wpres->outerr = value;
			break;
		case WPRES_exited_ok:
			wpres->exited_ok = atoi(value);
			break;
		case WPRES_error_msg:
			wpres->exited_ok = FALSE;
			wpres->error_msg = value;
			break;
		case WPRES_error_code:
			wpres->exited_ok = FALSE;
			wpres->error_code = atoi(value);
			break;
		case WPRES_runtime:
			/* ignored */
			break;
		case WPRES_ru_utime:
			str2timeval(value, &wpres->rusage.ru_utime);
			break;
		case WPRES_ru_stime:
			str2timeval(value, &wpres->rusage.ru_stime);
			break;
		case WPRES_ru_minflt:
			wpres->rusage.ru_minflt = atoi(value);
			break;
		case WPRES_ru_majflt:
			wpres->rusage.ru_majflt = atoi(value);
			break;
		case WPRES_ru_nswap:
			wpres->rusage.ru_nswap = atoi(value);
			break;
		case WPRES_ru_inblock:
			wpres->rusage.ru_inblock = atoi(value);
			break;
		case WPRES_ru_oublock:
			wpres->rusage.ru_oublock = atoi(value);
			break;
		case WPRES_ru_msgsnd:
			wpres->rusage.ru_msgsnd = atoi(value);
			break;
		case WPRES_ru_msgrcv:
			wpres->rusage.ru_msgrcv = atoi(value);
			break;
		case WPRES_ru_nsignals:
			wpres->rusage.ru_nsignals = atoi(value);
			break;
		case WPRES_ru_nvcsw:
			wpres->rusage.ru_nsignals = atoi(value);
			break;
		case WPRES_ru_nivcsw:
			wpres->rusage.ru_nsignals = atoi(value);
			break;

		default:
			nm_log(NSLOG_RUNTIME_WARNING, "wproc: Recognized but unhandled result variable: %s=%s\n", key, value);
			break;
		}
	}
	return 0;
}

static struct wproc_job *create_job(void (*callback)(struct wproc_result *, void *, int), void *data, time_t timeout, const char *cmd);
static int wproc_run_job(struct wproc_job *job, nagios_macros *mac);

static int handle_worker_result(int sd, int events, void *arg)
{
	char *buf, *error_reason = NULL;
	size_t size;
	int ret;
	unsigned int desired_workers;
	struct wproc_worker *wp = (struct wproc_worker *)arg;

	ret = nm_bufferqueue_read(wp->bq, wp->sd);

	if (ret < 0) {
		nm_log(NSLOG_RUNTIME_WARNING, "wproc: nm_bufferqueue_read() from %s returned %d: %s\n",
		       wp->name, ret, strerror(errno));
		return 0;
	} else if (ret == 0) {
		GHashTableIter iter;
		gpointer job_;
		nm_log(NSLOG_INFO_MESSAGE, "wproc: Socket to worker %s broken, removing", wp->name);
		wproc_num_workers_online--;
		iobroker_unregister(nagios_iobs, sd);

		/* remove worker from worker list - this ensures that we don't reassign
		 * its jobs back to itself*/
		remove_worker(wp);

		desired_workers = get_desired_workers(num_check_workers);

		if (workers.len < desired_workers) {
			/* there aren't global workers left, we can't run any more checks
			 * we should try respawning a few of the standard ones
			 */
			nm_log(NSLOG_RUNTIME_ERROR, "wproc: We have have less Core Workers than we should have, trying to respawn Core Worker");

			/* Respawn a worker */
			if ((ret = spawn_core_worker()) < 0) {
				nm_log(NSLOG_RUNTIME_ERROR, "wproc: Failed to respawn Core Worker");
			} else {
				nm_log(NSLOG_INFO_MESSAGE, "wproc: Respawning Core Worker %u was successful", ret);
			}
		} else if (workers.len == 0) {
			/* there aren't global workers left, we can't run any more checks
			 * this should never happen, because the respawning will be done in the upper if condition
			 */
			nm_log(NSLOG_RUNTIME_ERROR, "wproc: All our workers are dead, we can't do anything!");
		}

		/* reassign this dead worker's jobs */
		g_hash_table_iter_init(&iter, wp->jobs);
		while (g_hash_table_iter_next(&iter, NULL, &job_)) {
			struct wproc_job *job = job_;
			wproc_run_job(
			    create_job(job->callback, job->data, job->timeout, job->command),
			    NULL
			);
		}

		wproc_destroy(wp, WPROC_FORCE);
		return 0;
	}
	while ((buf = worker_ioc2msg(wp->bq, &size, 0))) {
		static struct kvvec kvv = KVVEC_INITIALIZER;
		struct wproc_job *job;
		wproc_result wpres;

		/* log messages are handled first */
		if (size > 5 && !memcmp(buf, "log=", 4)) {
			log_debug_info(DEBUGL_IPC, DEBUGV_BASIC, "wproc: %s: %s\n", wp->name, buf + 4);
			nm_free(buf);
			continue;
		}

		/* for everything else we need to actually parse */
		if (buf2kvvec_prealloc(&kvv, buf, size, '=', '\0', KVVEC_ASSIGN) <= 0) {
			nm_log(NSLOG_RUNTIME_ERROR,
			       "wproc: Failed to parse key/value vector from worker response with len %zd. First kv=%s",
			       size, buf ? buf : "(NULL)");
			nm_free(buf);
			continue;
		}

		memset(&wpres, 0, sizeof(wpres));
		wpres.job_id = -1;
		wpres.response = &kvv;
		wpres.source = wp->name;
		parse_worker_result(&wpres, &kvv);

		job = get_job(wp, wpres.job_id);
		if (!job) {
			nm_log(NSLOG_RUNTIME_WARNING, "wproc: Job with id '%d' doesn't exist on %s.\n", wpres.job_id, wp->name);
			nm_free(buf);
			continue;
		}

		/*
		 * ETIME ("Timer expired") doesn't really happen
		 * on any modern systems, so we reuse it to mean
		 * "program timed out"
		 */
		if (wpres.error_code == ETIME) {
			wpres.early_timeout = TRUE;
		}

		if (wpres.early_timeout) {
			nm_asprintf(&error_reason, "timed out after %.2fs", tv_delta_f(&wpres.start, &wpres.stop));
		} else if (WIFSIGNALED(wpres.wait_status)) {
			nm_asprintf(&error_reason, "died by signal %d%s after %.2f seconds",
			            WTERMSIG(wpres.wait_status),
			            WCOREDUMP(wpres.wait_status) ? " (core dumped)" : "",
			            tv_delta_f(&wpres.start, &wpres.stop));
		}
		if (error_reason) {
			log_debug_info(DEBUGL_IPC, DEBUGV_BASIC, "wproc: job %d from worker %s %s\n",
			               job->id, wp->name, error_reason);
			log_debug_info(DEBUGL_IPC, DEBUGV_MORE, "wproc:   command: %s\n", job->command);
			log_debug_info(DEBUGL_IPC, DEBUGV_MORE, "wproc:   early_timeout=%d; exited_ok=%d; wait_status=%d; error_code=%d;\n",
			               wpres.early_timeout, wpres.exited_ok, wpres.wait_status, wpres.error_code);
			wproc_logdump_buffer(DEBUGL_IPC, DEBUGV_MORE, "wproc:   stderr", wpres.outerr);
			wproc_logdump_buffer(DEBUGL_IPC, DEBUGV_MORE, "wproc:   stdout", wpres.outstd);
		}
		nm_free(error_reason);

		run_job_callback(job, &wpres, 0);
		g_hash_table_remove(wp->jobs, GINT_TO_POINTER(job->id));
		nm_free(buf);
	}

	return 0;
}

int workers_alive(void)
{
	unsigned int i;
	int alive = 0;

	for (i = 0; i < workers.len; i++) {
		if (wproc_is_alive(workers.wps[i]))
			alive++;
	}

	return alive;
}

/* a service for registering workers */
static int register_worker(int sd, char *buf, unsigned int len)
{
	int i, is_global = 1;
	struct kvvec *info;
	struct wproc_worker *worker;

	g_return_val_if_fail(specialized_workers != NULL, ERROR);

	log_debug_info(DEBUGL_IPC, DEBUGV_BASIC, "wproc: Registry request: %s\n", buf);
	worker = nm_calloc(1, sizeof(*worker));
	info = buf2kvvec(buf, len, '=', ';', 0);
	if (info == NULL) {
		free(worker);
		nm_log(NSLOG_RUNTIME_ERROR, "wproc: Failed to parse registration request\n");
		return 500;
	}

	worker->sd = sd;
	worker->bq = nm_bufferqueue_create();

	iobroker_unregister(nagios_iobs, sd);
	iobroker_register(nagios_iobs, sd, worker, handle_worker_result);

	for (i = 0; i < info->kv_pairs; i++) {
		struct key_value *kv = &info->kv[i];
		if (!strcmp(kv->key, "name")) {
			worker->name = nm_strdup(kv->value);
		} else if (!strcmp(kv->key, "pid")) {
			worker->pid = atoi(kv->value);
		} else if (!strcmp(kv->key, "max_jobs")) {
			worker->max_jobs = atoi(kv->value);
		} else if (!strcmp(kv->key, "plugin")) {
			struct wproc_list *command_handlers;
			is_global = 0;
			if (!(command_handlers = g_hash_table_lookup(specialized_workers, kv->value))) {
				command_handlers = nm_calloc(1, sizeof(struct wproc_list));
				command_handlers->wps = nm_calloc(1, sizeof(struct wproc_worker **));
				command_handlers->len = 1;
				command_handlers->wps[0] = worker;
				g_hash_table_insert(specialized_workers, nm_strdup(kv->value), command_handlers);
			} else {
				command_handlers->len++;
				command_handlers->wps = nm_realloc(command_handlers->wps, command_handlers->len * sizeof(struct wproc_worker **));
				command_handlers->wps[command_handlers->len - 1] = worker;
			}
			worker->wp_list = command_handlers;
		}
	}

	if (!worker->max_jobs) {
		/*
		 * each default worker uses two filedescriptors per job, one to
		 * connect to the master and about 13 to handle libraries
		 * and memory allocation, so this guesstimate shouldn't
		 * be too far off (for local workers, at least).
		 */
		worker->max_jobs = (iobroker_max_usable_fds() / 2) - 50;
	}

	worker->jobs = g_hash_table_new_full(
	                   g_direct_hash, g_direct_equal,
	                   NULL, destroy_job);

	if (is_global) {
		workers.len++;
		workers.wps = nm_realloc(workers.wps, workers.len * sizeof(struct wproc_worker *));
		workers.wps[workers.len - 1] = worker;
		worker->wp_list = &workers;
	}
	wproc_num_workers_online++;
	kvvec_destroy(info, 0);
	nsock_printf_nul(sd, "OK");

	/* signal query handler to release its bufferqueue for this one */
	return QH_TAKEOVER;
}

static int wproc_query_handler(int sd, char *buf, unsigned int len)
{
	char *space, *rbuf = NULL;

	if (!*buf || !strcmp(buf, "help")) {
		nsock_printf_nul(sd, "Control worker processes.\n"
		                 "Valid commands:\n"
		                 "  wpstats              Print general job information\n"
		                 "  register <options>   Register a new worker\n"
		                 "                       <options> can be name, pid, max_jobs and/or plugin.\n"
		                 "                       There can be many plugin args.");
		return 0;
	}

	if ((space = memchr(buf, ' ', len)) != NULL)
		* space = 0;

	rbuf = space ? space + 1 : buf;
	len -= (unsigned long)rbuf - (unsigned long)buf;

	if (!strcmp(buf, "register"))
		return register_worker(sd, rbuf, len);
	if (!strcmp(buf, "wpstats")) {
		unsigned int i;

		for (i = 0; i < workers.len; i++) {
			struct wproc_worker *wp = workers.wps[i];
			nsock_printf(sd, "name=%s;pid=%d;jobs_running=%u;jobs_started=%u\n",
			             wp->name, wp->pid,
			             g_hash_table_size(wp->jobs), wp->jobs_started);
		}
		return 0;
	}

	return 400;
}

static int spawn_core_worker(void)
{
	char *argvec[] = {naemon_binary_path, "--worker", qh_socket_path, NULL};
	int ret;

	if ((ret = spawn_helper(argvec)) < 0)
		nm_log(NSLOG_RUNTIME_ERROR, "wproc: Failed to launch core worker: %s\n", strerror(errno));
	else
		wproc_num_workers_spawned++;

	return ret;
}


static int get_desired_workers(int desired_workers)
{
	if (desired_workers <= 0) {
		int cpus = online_cpus();

		if (desired_workers < 0) {
			desired_workers = cpus - desired_workers;
		}
		if (desired_workers <= 0) {
			desired_workers = cpus * 1.5;
			/* min 4 workers, as it's tested and known to work */
			if (desired_workers < 4)
				desired_workers = 4;
			else if (desired_workers > 48) {
				/* don't go crazy in NASA's network (1024 cores) */
				desired_workers = 48;
			}
		}
	}

	wproc_num_workers_desired = desired_workers;

	return desired_workers;
}


int init_workers(int desired_workers)
{
	int i;

	/*
	 * we register our query handler before launching workers,
	 * so other workers can join us whenever they're ready
	 */
	specialized_workers = g_hash_table_new_full(g_str_hash, g_str_equal,
	                      free, NULL
	                                           );
	if (!qh_register_handler("wproc", "Worker process management and info", 0, wproc_query_handler)) {
		log_debug_info(DEBUGL_IPC, DEBUGV_BASIC, "wproc: Successfully registered manager as @wproc with query handler\n");
	} else {
		nm_log(NSLOG_RUNTIME_ERROR, "wproc: Failed to register manager with query handler\n");
		return -1;
	}

	/* Get the number of workers we need */
	desired_workers = get_desired_workers(desired_workers);

	if (workers_alive() == desired_workers)
		return 0;

	/* can't shrink the number of workers (yet) */
	if (desired_workers < (int)workers.len)
		return -1;

	for (i = 0; i < desired_workers; i++)
		spawn_core_worker();

	return 0;
}


static struct wproc_job *create_job(void (*callback)(struct wproc_result *, void *, int), void *data, time_t timeout, const char *cmd)
{
	struct wproc_job *job;
	struct wproc_worker *wp;

	wp = get_worker(cmd);
	if (!wp)
		return NULL;

	job = nm_calloc(1, sizeof(*job));
	job->wp = wp;
	job->id = get_job_id(wp);
	job->callback = callback;
	job->data = data;
	job->timeout = timeout;
	job->command = nm_strdup(cmd);
	g_hash_table_insert(wp->jobs, GINT_TO_POINTER(job->id), job);
	return job;
}

/*
 * Handles adding the command and macros to the kvvec,
 * as well as shipping the command off to a designated
 * worker
 */
static int wproc_run_job(struct wproc_job *job, nagios_macros *mac)
{
	static struct kvvec kvv = KVVEC_INITIALIZER;
	struct kvvec_buf *kvvb;
	struct wproc_worker *wp;
	int ret, result = OK;

	if (!job || !job->wp)
		return ERROR;

	wp = job->wp;

	if (!kvvec_init(&kvv, 4))	/* job_id, command and timeout */
		return ERROR;

	kvvec_addkv_str(&kvv, "job_id", (char *)mkstr("%d", job->id));
	kvvec_addkv_str(&kvv, "type", "0");
	kvvec_addkv_str(&kvv, "command", job->command);
	kvvec_addkv_str(&kvv, "timeout", (char *)mkstr("%u", job->timeout));
	kvvb = build_kvvec_buf(&kvv);
	ret = iobroker_write_packet(nagios_iobs, wp->sd, kvvb->buf, kvvb->bufsize);
	if (ret < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "wproc: '%s' seems to be choked. ret = %d; bufsize = %lu: errno = %d (%s)\n",
		       wp->name, ret, kvvb->bufsize, errno, strerror(errno));
		g_hash_table_remove(wp->jobs, GINT_TO_POINTER(job->id));
		result = ERROR;
	} else {
		wp->jobs_started++;
	}
	nm_free(kvvb->buf);
	nm_free(kvvb);

	return result;
}

int wproc_run_callback(char *cmd, int timeout,
                       void (*cb)(struct wproc_result *, void *, int), void *data,
                       nagios_macros *mac)
{
	struct wproc_job *job;
	job = create_job(cb, data, timeout, cmd);
	return wproc_run_job(job, mac);
}
