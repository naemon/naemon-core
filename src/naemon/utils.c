#include "config.h"
#include "common.h"
#include "objects_command.h"
#include "objects_host.h"
#include "objects_hostescalation.h"
#include "objects_hostdependency.h"
#include "objects_service.h"
#include "objects_serviceescalation.h"
#include "objects_servicedependency.h"
#include "statusdata.h"
#include "comments.h"
#include "downtime.h"
#include "macros.h"
#include "broker.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "notifications.h"
#include "workers.h"
#include "utils.h"
#include "commands.h"
#include "events.h"
#include "logging.h"
#include "defaults.h"
#include "globals.h"
#include "nm_alloc.h"
#include "perfdata.h"
#include <assert.h>
#include <limits.h>
#include <sys/types.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <dirent.h>
#include <math.h>
#include <poll.h>
#include <string.h>

/* global varaiables only used by the daemon */
char *naemon_binary_path = NULL;
char *config_file = NULL;
char *command_file = NULL;
char *temp_file = NULL;
char *temp_path = NULL;
char *check_result_path = NULL;
char *lock_file = NULL;
objectlist *objcfg_files = NULL;
objectlist *objcfg_dirs = NULL;
int upipe_fd[2];

int num_check_workers = 0; /* auto-decide */
char *qh_socket_path = NULL; /* disabled */

char *ocsp_command = NULL;
char *ochp_command = NULL;
command *ocsp_command_ptr = NULL;
command *ochp_command_ptr = NULL;
int ocsp_timeout = DEFAULT_OCSP_TIMEOUT;
int ochp_timeout = DEFAULT_OCHP_TIMEOUT;

int use_regexp_matches;
int use_true_regexp_matching;

int	use_syslog = DEFAULT_USE_SYSLOG;
char *log_file = NULL;
char *log_archive_path = NULL;
int log_notifications = DEFAULT_NOTIFICATION_LOGGING;
int enable_notification_suppression_reason_logging = DEFAULT_NSR_LOGGING;
int log_service_retries = DEFAULT_LOG_SERVICE_RETRIES;
int log_host_retries = DEFAULT_LOG_HOST_RETRIES;
int log_event_handlers = DEFAULT_LOG_EVENT_HANDLERS;
int log_external_commands = DEFAULT_LOG_EXTERNAL_COMMANDS;
int log_passive_checks = DEFAULT_LOG_PASSIVE_CHECKS;
unsigned long logging_options = 0;
unsigned long syslog_options = 0;

int service_check_timeout = DEFAULT_SERVICE_CHECK_TIMEOUT;
int service_check_timeout_state = STATE_CRITICAL;
int host_check_timeout = DEFAULT_HOST_CHECK_TIMEOUT;
int event_handler_timeout = DEFAULT_EVENT_HANDLER_TIMEOUT;
int notification_timeout = DEFAULT_NOTIFICATION_TIMEOUT;


char *object_precache_file;

char *global_host_event_handler = NULL;
char *global_service_event_handler = NULL;
command *global_host_event_handler_ptr = NULL;
command *global_service_event_handler_ptr = NULL;

int check_reaper_interval = DEFAULT_CHECK_REAPER_INTERVAL;
int max_check_reaper_time = DEFAULT_MAX_REAPER_TIME;
int service_freshness_check_interval = DEFAULT_FRESHNESS_CHECK_INTERVAL;
int host_freshness_check_interval = DEFAULT_FRESHNESS_CHECK_INTERVAL;

int check_orphaned_services = DEFAULT_CHECK_ORPHANED_SERVICES;
int check_orphaned_hosts = DEFAULT_CHECK_ORPHANED_HOSTS;
int check_service_freshness = DEFAULT_CHECK_SERVICE_FRESHNESS;
int check_host_freshness = DEFAULT_CHECK_HOST_FRESHNESS;

int additional_freshness_latency = DEFAULT_ADDITIONAL_FRESHNESS_LATENCY;

time_t last_program_stop = 0L;

int use_aggressive_host_checking = DEFAULT_AGGRESSIVE_HOST_CHECKING;
time_t cached_host_check_horizon = DEFAULT_CACHED_HOST_CHECK_HORIZON;
time_t cached_service_check_horizon = DEFAULT_CACHED_SERVICE_CHECK_HORIZON;
int enable_predictive_host_dependency_checks = DEFAULT_ENABLE_PREDICTIVE_HOST_DEPENDENCY_CHECKS;
int enable_predictive_service_dependency_checks = DEFAULT_ENABLE_PREDICTIVE_SERVICE_DEPENDENCY_CHECKS;

int soft_state_dependencies = FALSE;

int retain_state_information = FALSE;
int retention_update_interval = DEFAULT_RETENTION_UPDATE_INTERVAL;
int use_retained_program_state = TRUE;
int use_retained_scheduling_info = FALSE;
int retained_scheduling_randomize_window = DEFAULT_RETAINED_SCHEDULING_RANDOMIZE_WINDOW;
int retention_scheduling_horizon = DEFAULT_RETENTION_SCHEDULING_HORIZON;
char *retention_file = NULL;

unsigned long modified_process_attributes = MODATTR_NONE;
unsigned long modified_host_process_attributes = MODATTR_NONE;
unsigned long modified_service_process_attributes = MODATTR_NONE;
unsigned long retained_host_attribute_mask = 0L;
unsigned long retained_service_attribute_mask = 0L;
unsigned long retained_contact_host_attribute_mask = 0L;
unsigned long retained_contact_service_attribute_mask = 0L;
unsigned long retained_process_host_attribute_mask = 0L;
unsigned long retained_process_service_attribute_mask = 0L;

unsigned long next_event_id = 0L;
unsigned long next_comment_id = 0L;

int verify_config = FALSE;
int precache_objects = FALSE;
int use_precached_objects = FALSE;

volatile sig_atomic_t sigshutdown = FALSE;
volatile sig_atomic_t sigrestart = FALSE;
volatile sig_atomic_t sigrotate = FALSE;
volatile sig_atomic_t sigfilesize = FALSE;
volatile sig_atomic_t sig_id = 0;

int max_parallel_service_checks = DEFAULT_MAX_PARALLEL_SERVICE_CHECKS;
int currently_running_service_checks = 0;
int currently_running_host_checks = 0;

time_t event_start = 0L;

int translate_passive_host_checks = DEFAULT_TRANSLATE_PASSIVE_HOST_CHECKS;
int passive_host_checks_are_soft = DEFAULT_PASSIVE_HOST_CHECKS_SOFT;

int status_update_interval = DEFAULT_STATUS_UPDATE_INTERVAL;

int time_change_threshold = DEFAULT_TIME_CHANGE_THRESHOLD;

unsigned long   event_broker_options = BROKER_NOTHING;

double low_service_flap_threshold = DEFAULT_LOW_SERVICE_FLAP_THRESHOLD;
double high_service_flap_threshold = DEFAULT_HIGH_SERVICE_FLAP_THRESHOLD;
double low_host_flap_threshold = DEFAULT_LOW_HOST_FLAP_THRESHOLD;
double high_host_flap_threshold = DEFAULT_HIGH_HOST_FLAP_THRESHOLD;

char *use_timezone = NULL;

int allow_empty_hostgroup_assignment = DEFAULT_ALLOW_EMPTY_HOSTGROUP_ASSIGNMENT;
int allow_circular_dependencies = DEFAULT_ALLOW_CIRCULAR_DEPENDENCIES;
int host_down_disable_service_checks = DEFAULT_HOST_DOWN_DISABLE_SERVICE_CHECKS;
int service_parents_disable_service_checks = DEFAULT_SERVICE_PARENTS_DISABLE_SERVICE_CHECKS;
int service_skip_check_dependency_status = DEFAULT_SKIP_CHECK_STATUS;
int service_skip_check_host_down_status = DEFAULT_SKIP_CHECK_STATUS;
int host_skip_check_dependency_status = DEFAULT_SKIP_CHECK_STATUS;

static long long check_file_size(char *, unsigned long, struct rlimit);

time_t max_check_result_file_age = DEFAULT_MAX_CHECK_RESULT_AGE;

check_stats     check_statistics[MAX_CHECK_STATS_TYPES];

char *debug_file;
int debug_level = DEFAULT_DEBUG_LEVEL;
int debug_verbosity = DEFAULT_DEBUG_VERBOSITY;
unsigned long   max_debug_file_size = DEFAULT_MAX_DEBUG_FILE_SIZE;

/* from GNU defines errno as a macro, since it's a per-thread variable */
#ifndef errno
extern int errno;
#endif


static const char *worker_source_name(void *source)
{
	return source ? (const char *)source : "unknown internal source (voodoo, perhaps?)";
}

struct check_engine nagios_check_engine = {
	"Naemon Core",
	worker_source_name,
	NULL,
};

const char *check_result_source(check_result *cr)
{
	if (cr->engine)
		return cr->engine->source_name(cr->source);
	return cr->source ? (const char *)cr->source : "(unknown engine)";
}

/******************************************************************/
/******************** ENVIRONMENT FUNCTIONS ***********************/
/******************************************************************/

/* sets or unsets an environment variable */
int set_environment_var(char *name, char *value, int set)
{
#ifndef HAVE_SETENV
	char *env_string = NULL;
#endif

	/* we won't mess with null variable names */
	if (name == NULL)
		return ERROR;

	/* set the environment variable */
	if (set == TRUE) {

#ifdef HAVE_SETENV
		setenv(name, (value == NULL) ? "" : value, 1);
#else
		/* needed for Solaris and systems that don't have setenv() */
		/* this will leak memory, but in a "controlled" way, since lost memory should be freed when the child process exits */
		nm_asprintf(&env_string, "%s=%s", name, (value == NULL) ? "" : value);
		if (env_string)
			putenv(env_string);
#endif
	}
	/* clear the variable */
	else {
#ifdef HAVE_UNSETENV
		unsetenv(name);
#endif
	}

	return OK;
}


/******************************************************************/
/******************** SIGNAL HANDLER FUNCTIONS ********************/
/******************************************************************/

/* trap signals so we can exit gracefully */
void setup_sighandler(void)
{
	size_t i;
	struct sigaction sigact;
	int handled_signals[] = {
		SIGQUIT,
		SIGTERM,
		SIGHUP,
		SIGUSR1,
		SIGINT
	};

	/* remove buffering from stderr, stdin, and stdout */
	setbuf(stdin, (char *)NULL);
	setbuf(stdout, (char *)NULL);
	setbuf(stderr, (char *)NULL);

	sigact.sa_handler = &sighandler;
	g_warn_if_fail(sigemptyset(&(sigact.sa_mask)) == 0);
	sigact.sa_flags = 0;

	/* initialize signal handling */
	signal(SIGPIPE, SIG_IGN);

	for (i = 0; i < (sizeof(handled_signals) / sizeof(handled_signals[0])); ++i) {
		if (sigaction(handled_signals[i], &sigact, NULL) < 0) {
			nm_log(NSLOG_RUNTIME_ERROR, "Failed to set signal handler for '%s': %s", strsignal(handled_signals[i]), strerror(errno));
		}
	}
}


/* reset signal handling... */
void reset_sighandler(void)
{
	size_t i;
	/* set signal handling to default actions */
	int signals[] = {
		SIGQUIT,
		SIGTERM,
		SIGHUP,
		SIGPIPE,
		SIGXFSZ,
		SIGUSR1,
		SIGINT
	};

	for (i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
		if (signal(signals[i], SIG_DFL) == SIG_ERR) {
			nm_log(NSLOG_RUNTIME_ERROR,
			       "Failed to reset signal handler for %s: %s",
			       strsignal(signals[i]), strerror(errno));
		}
	}
}


/* handle signals */
void sighandler(int sig)
{
	if (sig <= 0)
		return;

	sig_id = sig;
	switch (sig_id) {
	case SIGHUP: sigrestart = TRUE; break;
	case SIGXFSZ: sigfilesize = TRUE; break;
	case SIGUSR1: sigrotate = TRUE; break;
	case SIGQUIT: /* fallthrough */
	case SIGTERM: /* fallthrough */
	case SIGINT: /* fallthrough */
	case SIGPIPE: sigshutdown = TRUE;
	}
}

void signal_react()
{
	int signum = sig_id;
	if (signum <= 0)
		return;

	if (sigrestart) {
		/* we received a SIGHUP, so restart... */
		nm_log(NSLOG_PROCESS_INFO, "Caught '%s', restarting...\n", strsignal(signum));
	} else if (sigfilesize) {
		handle_sigxfsz();
	} else if (sigshutdown) {
		/* else begin shutting down... */
		nm_log(NSLOG_PROCESS_INFO, "Caught '%s', shutting down...\n", strsignal(signum));
	}
	sig_id = 0;
}


/**
 * Handle the SIGXFSZ signal. A SIGXFSZ signal is received when a file exceeds
 * the maximum allowable size either as dictated by the fzise parameter in
 * /etc/security/limits.conf (ulimit -f) or by the maximum size allowed by
 * the filesystem
 */
void handle_sigxfsz()
{
	/* TODO: This doesn't really do anything worthwhile in most cases. The "right" thing
	 * to do would probably be to rotate out the offending file, if feasible. Just ignoring
	 * the problem is not likely to work.
	 */

	static time_t lastlog_time = (time_t)0; /* Save the last log time so we
	                                           don't log too often. */
	unsigned long log_interval = 300; /* How frequently to log messages
	                                     about receiving the signal */
	struct rlimit rlim;
	time_t now;
	char *files[] = {
		log_file,
		debug_file,
		host_perfdata_file,
		service_perfdata_file,
		object_cache_file,
		object_precache_file,
		status_file,
		retention_file,
	};
	int x;
	char **filep;
	long long size;
	long long max_size = 0LL;
	char *max_name = NULL;

	/* Check the current time and if less time has passed since the last
	   time the signal was received, ignore it */
	time(&now);
	if ((unsigned long)(now - lastlog_time) < log_interval)
		return;

	/* Get the current file size limit */
	if (getrlimit(RLIMIT_FSIZE, &rlim) != 0) {
		/* Attempt to log the error, realizing that the logging may fail
		   if it is the log file that is over the size limit. */
		nm_log(NSLOG_RUNTIME_ERROR, "Unable to determine current resource limits: %s\n",
		       strerror(errno));
		lastlog_time = now;
		return;
	}

	/* Try to figure out which file caused the signal and react
	   appropriately */
	for (x = 0, filep = files; (size_t)x < (sizeof(files) / sizeof(files[0]));
	     x++, filep++) {

		if (*filep == NULL)
			continue;

		if ((size = check_file_size(*filep, 1024, rlim)) == -1) {
			lastlog_time = now;
			return;
		} else if (size > max_size) {
			max_size = size;
			max_name = *filep;
		}
	}

	if ((max_size > 0) && (max_name != NULL)) {
		nm_log(NSLOG_RUNTIME_ERROR, "SIGXFSZ received because a "
		       "file's size may have exceeded the file size limits of "
		       "the filesystem. The largest file checked, '%s', has a "
		       "size of %lld bytes", max_name, max_size);

	} else {
		nm_log(NSLOG_RUNTIME_ERROR, "SIGXFSZ received but unable to "
		       "determine which file may have caused it.");
	}
}


/**
 * Checks a file to determine whether it exceeds resource limit imposed
 * limits. Returns the file size if file is OK, 0 if it's status could not
 * be determined, or -1 if not OK. fudge is the fudge factor (in bytes) for
 * checking the file size
 */
static long long check_file_size(char *path, unsigned long fudge,
                                 struct rlimit rlim)
{

	struct stat status;

	/* Make sure we were passed a legitimate file path */
	if (NULL == path)
		return 0;

	/* Get the status of the file */
	if (stat(path, &status) < 0) {
		nm_log(NSLOG_RUNTIME_ERROR,
		       "Unable to determine status of file %s: %s\n",
		       path, strerror(errno));
		return 0;
	}

	/* Make sure it is a file */
	if (!S_ISREG(status.st_mode))
		return 0;

	/* file size doesn't reach limit, just returns it */
	if (status.st_size + fudge <= rlim.rlim_cur)
		return status.st_size;

	/* If the file size plus the fudge factor exceeds the
	   current resource limit imposed size limit, log an error */
	nm_log(NSLOG_RUNTIME_ERROR, "Size of file '%s' (%llu) "
	       "exceeds (or nearly exceeds) size imposed by resource "
	       "limits (%llu). Consider increasing limits with "
	       "ulimit(1).\n", path,
	       (unsigned long long)status.st_size,
	       (unsigned long long)rlim.rlim_cur);
	return -1;

}


/******************************************************************/
/************************ DAEMON FUNCTIONS ************************/
/******************************************************************/
static int set_working_directory(void)
{
	/*
	 * we shouldn't block the unmounting of
	 * filesystems, so chdir() to the root
	 */
	if (chdir("/") != 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Aborting. Failed to set daemon working directory (/): %s\n", strerror(errno));
		return (ERROR);
	}
	return (OK);
}

int signal_parent(int sig)
{
	if (write(upipe_fd[PIPE_WRITE], &sig, sizeof(int)) < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Failed to signal parent: %s",
		       strerror(errno));
		return ERROR;
	}
	return OK;
}

int daemon_init(void)
{
	int pid = 0;
	int lockfile = 0;
	int val = 0;
	char buf[256];
	struct flock lock;

	if (set_working_directory() == (ERROR)) {
		return (ERROR);
	}

	umask(S_IWGRP | S_IWOTH);

	lockfile = open(lock_file, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

	if (lockfile < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Failed to obtain lock on file %s: %s\n", lock_file, strerror(errno));
		nm_log(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, "Bailing out due to errors encountered while attempting to daemonize... (PID=%d)", (int)getpid());
		return (ERROR);
	}

	/* see if we can read the contents of the lockfile */
	if ((val = read(lockfile, buf, (size_t)10)) < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Lockfile exists but cannot be read");
		return (ERROR);
	}

	/* we read something - check the PID */
	if (val > 0) {
		if ((val = sscanf(buf, "%d", &pid)) < 1) {
			nm_log(NSLOG_RUNTIME_ERROR, "Lockfile '%s' does not contain a valid PID (%s)", lock_file, buf);
			return (ERROR);
		}
	}

	/* check for SIGHUP */
	if (val == 1 && pid == (int)getpid()) {
		return OK;
	} else {

		lock.l_type = F_WRLCK;
		lock.l_start = 0;
		lock.l_whence = SEEK_SET;
		lock.l_len = 0;
		if (fcntl(lockfile, F_GETLK, &lock) == -1) {
			nm_log(NSLOG_RUNTIME_ERROR, "Failed to access lockfile '%s'. %s. Bailing out...", lock_file, strerror(errno));
			return (ERROR);
		}

		if (lock.l_type != F_UNLCK) {
			nm_log(NSLOG_RUNTIME_ERROR, "Lockfile '%s' looks like its already held by another instance of Naemon (PID %d).  Bailing out, pre-fork...", lock_file, (int)lock.l_pid);
			return (ERROR);
		}
	}

	/*
	 * set up a pipe used for sending a message from the child to the parent
	 * when initialization is complete.
	 */
	if (pipe(upipe_fd) < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Failed to set up unnamned pipe: %s",
		       strerror(errno));
		return (ERROR);
	}

	if ((pid = (int)fork()) < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Unable to fork out the daemon process: %s",
		       strerror(errno));
		return (ERROR);
	} else if (pid != 0) {
		/* parent stops - when child is done, we exit here */
		int return_code = EXIT_FAILURE;
		if (close(upipe_fd[PIPE_WRITE]) < 0) {
			nm_log(NSLOG_RUNTIME_ERROR, "Unable to close parent write end: %s",
			       strerror(errno));
			return_code = EXIT_FAILURE;
		}
		/*
		 * wait for child to send the OK return code, if the child dies
		 * we stop blocking and the return code remains EXIT_FAILURE.
		 */
		if (read(upipe_fd[PIPE_READ], &return_code, sizeof(int)) < 0) {
			nm_log(NSLOG_RUNTIME_ERROR, "Unable to read from pipe: %s",
			       strerror(errno));
			return_code = EXIT_FAILURE;
		}
		if (close(upipe_fd[PIPE_READ]) < 0) {
			nm_log(NSLOG_RUNTIME_ERROR, "Unable to close parent read end: %s",
			       strerror(errno));
			return_code = EXIT_FAILURE;
		}
		if (return_code != OK) {
			/* signal the child to die in case an error occurs in parent */
			kill(pid, SIGTERM);
		}
		exit(return_code);
	} else {
		/* close read end of the pipe in the child */
		if (close(upipe_fd[PIPE_READ]) < 0) {
			nm_log(NSLOG_RUNTIME_ERROR, "Unable to close child read end: %s",
			       strerror(errno));
			return ERROR;
		}
	}

	/* child becomes session leader... */
	setsid();
	/* place a file lock on the lock file */
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;
	lock.l_pid = getpid();
	if (fcntl(lockfile, F_SETLK, &lock) == -1) {
		if (errno == EACCES || errno == EAGAIN) {
			fcntl(lockfile, F_GETLK, &lock);
			nm_log(NSLOG_RUNTIME_ERROR, "Lockfile '%s' looks like its already held by another instance of Naemon (PID %d).  Bailing out, post-fork...", lock_file, (int)lock.l_pid);
		} else
			nm_log(NSLOG_RUNTIME_ERROR, "Cannot lock lockfile '%s': %s. Bailing out...", lock_file, strerror(errno));

		return (ERROR);
	}

	/* write PID to lockfile... */
	lseek(lockfile, 0, SEEK_SET);
	if (ftruncate(lockfile, 0) != 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Cannot truncate lockfile '%s': %s. Bailing out...", lock_file, strerror(errno));
		return (ERROR);
	}
	sprintf(buf, "%d\n", (int)getpid());

	if (nsock_write_all(lockfile, buf, strlen(buf)) != 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Cannot write PID to lockfile '%s': %s. Bailing out...", lock_file, strerror(errno));
		return (ERROR);
	}

	/* make sure lock file stays open while program is executing... */
	val = fcntl(lockfile, F_GETFD, 0);
	val |= FD_CLOEXEC;
	fcntl(lockfile, F_SETFD, val);

	broker_program_state(NEBTYPE_PROCESS_DAEMONIZE, NEBFLAG_NONE, NEBATTR_NONE);

	return OK;
}

/******************************************************************/
/************************* FILE FUNCTIONS *************************/
/******************************************************************/

/* renames a file */
int my_rename(char *source, char *dest)
{
	int rename_result = 0;


	/* make sure we have something */
	if (source == NULL || dest == NULL)
		return -1;

	/* first see if we can rename file with standard function */
	rename_result = rename(source, dest);

	/* handle any errors... */
	if (rename_result == -1) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Unable to rename file '%s' to '%s': %s\n", source, dest, strerror(errno));
		return rename_result;
	}

	return rename_result;
}

/******************************************************************/
/********************** CHECK STATS FUNCTIONS *********************/
/******************************************************************/

/* initialize check statistics data structures */
int init_check_stats(void)
{
	int x = 0;
	int y = 0;

	for (x = 0; x < MAX_CHECK_STATS_TYPES; x++) {
		check_statistics[x].current_bucket = 0;
		for (y = 0; y < CHECK_STATS_BUCKETS; y++)
			check_statistics[x].bucket[y] = 0;
		check_statistics[x].overflow_bucket = 0;
		for (y = 0; y < 3; y++)
			check_statistics[x].minute_stats[y] = 0;
		check_statistics[x].last_update = (time_t)0L;
	}

	return OK;
}


/* records stats for a given type of check */
int update_check_stats(int check_type, time_t check_time)
{
	time_t current_time;
	unsigned long minutes = 0L;
	int new_current_bucket = 0;
	int this_bucket = 0;
	int x = 0;

	if (check_type < 0 || check_type >= MAX_CHECK_STATS_TYPES)
		return ERROR;

	time(&current_time);

	if ((unsigned long)check_time == 0L) {
#ifdef DEBUG_CHECK_STATS
		printf("TYPE[%d] CHECK TIME==0!\n", check_type);
#endif
		check_time = current_time;
	}

	/* do some sanity checks on the age of the stats data before we start... */
	/* get the new current bucket number */
	minutes = ((unsigned long)check_time - (unsigned long)program_start) / 60;
	new_current_bucket = minutes % CHECK_STATS_BUCKETS;

	/* its been more than 15 minutes since stats were updated, so clear the stats */
	if ((((unsigned long)current_time - (unsigned long)check_statistics[check_type].last_update) / 60) > CHECK_STATS_BUCKETS) {
		for (x = 0; x < CHECK_STATS_BUCKETS; x++)
			check_statistics[check_type].bucket[x] = 0;
		check_statistics[check_type].overflow_bucket = 0;
#ifdef DEBUG_CHECK_STATS
		printf("CLEARING ALL: TYPE[%d], CURRENT=%lu, LASTUPDATE=%lu\n", check_type, (unsigned long)current_time, (unsigned long)check_statistics[check_type].last_update);
#endif
	}

	/* different current bucket number than last time */
	else if (new_current_bucket != check_statistics[check_type].current_bucket) {

		/* clear stats in buckets between last current bucket and new current bucket - stats haven't been updated in a while */
		for (x = check_statistics[check_type].current_bucket; x < (CHECK_STATS_BUCKETS * 2); x++) {

			this_bucket = (x + CHECK_STATS_BUCKETS + 1) % CHECK_STATS_BUCKETS;

			if (this_bucket == new_current_bucket)
				break;

#ifdef DEBUG_CHECK_STATS
			printf("CLEARING BUCKET %d, (NEW=%d, OLD=%d)\n", this_bucket, new_current_bucket, check_statistics[check_type].current_bucket);
#endif

			/* clear old bucket value */
			check_statistics[check_type].bucket[this_bucket] = 0;
		}

		/* update the current bucket number, push old value to overflow bucket */
		check_statistics[check_type].overflow_bucket = check_statistics[check_type].bucket[new_current_bucket];
		check_statistics[check_type].current_bucket = new_current_bucket;
		check_statistics[check_type].bucket[new_current_bucket] = 0;
	}
#ifdef DEBUG_CHECK_STATS
	else
		printf("NO CLEARING NEEDED\n");
#endif


	/* increment the value of the current bucket */
	check_statistics[check_type].bucket[new_current_bucket]++;

#ifdef DEBUG_CHECK_STATS
	printf("TYPE[%d].BUCKET[%d]=%d\n", check_type, new_current_bucket, check_statistics[check_type].bucket[new_current_bucket]);
	printf("   ");
	for (x = 0; x < CHECK_STATS_BUCKETS; x++)
		printf("[%d] ", check_statistics[check_type].bucket[x]);
	printf(" (%d)\n", check_statistics[check_type].overflow_bucket);
#endif

	/* record last update time */
	check_statistics[check_type].last_update = current_time;

	return OK;
}


/* generate 1/5/15 minute stats for a given type of check */
int generate_check_stats(void)
{
	time_t current_time;
	int x = 0;
	int new_current_bucket = 0;
	int this_bucket = 0;
	int last_bucket = 0;
	int this_bucket_value = 0;
	int last_bucket_value = 0;
	int bucket_value = 0;
	int seconds = 0;
	int minutes = 0;
	int check_type = 0;
	float this_bucket_weight = 0.0;
	float last_bucket_weight = 0.0;

	time(&current_time);

	/* do some sanity checks on the age of the stats data before we start... */
	/* get the new current bucket number */
	minutes = ((unsigned long)current_time - (unsigned long)program_start) / 60;
	new_current_bucket = minutes % CHECK_STATS_BUCKETS;
	for (check_type = 0; check_type < MAX_CHECK_STATS_TYPES; check_type++) {

		/* its been more than 15 minutes since stats were updated, so clear the stats */
		if ((((unsigned long)current_time - (unsigned long)check_statistics[check_type].last_update) / 60) > CHECK_STATS_BUCKETS) {
			for (x = 0; x < CHECK_STATS_BUCKETS; x++)
				check_statistics[check_type].bucket[x] = 0;
			check_statistics[check_type].overflow_bucket = 0;
#ifdef DEBUG_CHECK_STATS
			printf("GEN CLEARING ALL: TYPE[%d], CURRENT=%lu, LASTUPDATE=%lu\n", check_type, (unsigned long)current_time, (unsigned long)check_statistics[check_type].last_update);
#endif
		}

		/* different current bucket number than last time */
		else if (new_current_bucket != check_statistics[check_type].current_bucket) {

			/* clear stats in buckets between last current bucket and new current bucket - stats haven't been updated in a while */
			for (x = check_statistics[check_type].current_bucket; x < (CHECK_STATS_BUCKETS * 2); x++) {

				this_bucket = (x + CHECK_STATS_BUCKETS + 1) % CHECK_STATS_BUCKETS;

				if (this_bucket == new_current_bucket)
					break;

#ifdef DEBUG_CHECK_STATS
				printf("GEN CLEARING BUCKET %d, (NEW=%d, OLD=%d), CURRENT=%lu, LASTUPDATE=%lu\n", this_bucket, new_current_bucket, check_statistics[check_type].current_bucket, (unsigned long)current_time, (unsigned long)check_statistics[check_type].last_update);
#endif

				/* clear old bucket value */
				check_statistics[check_type].bucket[this_bucket] = 0;
			}

			/* update the current bucket number, push old value to overflow bucket */
			check_statistics[check_type].overflow_bucket = check_statistics[check_type].bucket[new_current_bucket];
			check_statistics[check_type].current_bucket = new_current_bucket;
			check_statistics[check_type].bucket[new_current_bucket] = 0;
		}
#ifdef DEBUG_CHECK_STATS
		else
			printf("GEN NO CLEARING NEEDED: TYPE[%d], CURRENT=%lu, LASTUPDATE=%lu\n", check_type, (unsigned long)current_time, (unsigned long)check_statistics[check_type].last_update);
#endif

		/* update last check time */
		check_statistics[check_type].last_update = current_time;
	}

	/* determine weights to use for this/last buckets */
	seconds = ((unsigned long)current_time - (unsigned long)program_start) % 60;
	this_bucket_weight = (seconds / 60.0);
	last_bucket_weight = ((60 - seconds) / 60.0);

	/* update statistics for all check types */
	for (check_type = 0; check_type < MAX_CHECK_STATS_TYPES; check_type++) {

		/* clear the old statistics */
		for (x = 0; x < 3; x++)
			check_statistics[check_type].minute_stats[x] = 0;

		/* loop through each bucket */
		for (x = 0; x < CHECK_STATS_BUCKETS; x++) {

			/* which buckets should we use for this/last bucket? */
			this_bucket = (check_statistics[check_type].current_bucket + CHECK_STATS_BUCKETS - x) % CHECK_STATS_BUCKETS;
			last_bucket = (this_bucket + CHECK_STATS_BUCKETS - 1) % CHECK_STATS_BUCKETS;

			/* raw/unweighted value for this bucket */
			this_bucket_value = check_statistics[check_type].bucket[this_bucket];

			/* raw/unweighted value for last bucket - use overflow bucket if last bucket is current bucket */
			if (last_bucket == check_statistics[check_type].current_bucket)
				last_bucket_value = check_statistics[check_type].overflow_bucket;
			else
				last_bucket_value = check_statistics[check_type].bucket[last_bucket];

			/* determine value by weighting this/last buckets... */
			/* if this is the current bucket, use its full value + weighted % of last bucket */
			if (x == 0) {
				bucket_value = (int)(this_bucket_value + floor((double)last_bucket_value * last_bucket_weight));
			}
			/* otherwise use weighted % of this and last bucket */
			else {
				bucket_value = (int)(ceil((double)this_bucket_value * this_bucket_weight) + floor((double)last_bucket_value * (double)last_bucket_weight));
			}

			/* 1 minute stats */
			if (x == 0)
				check_statistics[check_type].minute_stats[0] = bucket_value;

			/* 5 minute stats */
			if (x < 5)
				check_statistics[check_type].minute_stats[1] += bucket_value;

			/* 15 minute stats */
			if (x < 15)
				check_statistics[check_type].minute_stats[2] += bucket_value;

#ifdef DEBUG_CHECK_STATS2
			printf("X=%d, THIS[%d]=%d, LAST[%d]=%d, 1/5/15=%d,%d,%d  L=%d R=%d\n", x, this_bucket, this_bucket_value, last_bucket, last_bucket_value, check_statistics[check_type].minute_stats[0], check_statistics[check_type].minute_stats[1], check_statistics[check_type].minute_stats[2], left_value, right_value);
#endif
			/* record last update time */
			check_statistics[check_type].last_update = current_time;
		}

#ifdef DEBUG_CHECK_STATS
		printf("TYPE[%d]   1/5/15 = %d, %d, %d (seconds=%d, this_weight=%f, last_weight=%f)\n", check_type, check_statistics[check_type].minute_stats[0], check_statistics[check_type].minute_stats[1], check_statistics[check_type].minute_stats[2], seconds, this_bucket_weight, last_bucket_weight);
#endif
	}

	return OK;
}


/******************************************************************/
/************************* MISC FUNCTIONS *************************/
/******************************************************************/

/* returns Naemon version */
const char *get_program_version(void)
{
	return (const char *)VERSION;
}


/******************************************************************/
/*********************** CLEANUP FUNCTIONS ************************/
/******************************************************************/

/* do some cleanup before we exit */
void cleanup(void)
{
	/* free event queue data */
	destroy_event_queue();

	/* unload modules */
	if (verify_config == FALSE) {
		neb_free_callback_list();
		neb_unload_all_modules(NEBMODULE_FORCE_UNLOAD, (sigshutdown == TRUE) ? NEBMODULE_NEB_SHUTDOWN : NEBMODULE_NEB_RESTART);
		neb_free_module_list();
		neb_deinit_modules();
	}

	/* free all allocated memory - including macros */
	free_memory(get_global_macros());
	close_log_file();

	return;
}


/* free the memory allocated to the linked lists */
void free_memory(nagios_macros *mac)
{
	int i;
	objectlist *entry, *next;

	destroy_objects_command();
	destroy_objects_timeperiod();
	destroy_objects_host();
	destroy_objects_service(TRUE);
	destroy_objects_contact();
	destroy_objects_contactgroup();
	destroy_objects_hostgroup();
	destroy_objects_servicegroup(TRUE);

	free_comment_data();
	free_downtime_data();

	nm_free(global_host_event_handler);
	nm_free(global_service_event_handler);

	/* free obsessive compulsive commands */
	nm_free(ocsp_command);
	nm_free(ochp_command);

	nm_free(object_cache_file);
	nm_free(object_precache_file);

	/*
	 * free memory associated with macros.
	 * It's ok to only free the volatile ones, as the non-volatile
	 * are always free()'d before assignment if they're set.
	 * Doing a full free of them here means we'll wipe the constant
	 * macros when we get a reload or restart request through the
	 * command pipe, or when we receive a SIGHUP.
	 */
	clear_volatile_macros_r(mac);

	free_macrox_names();

	for (entry = objcfg_files; entry; entry = next) {
		next = entry->next;
		nm_free(entry->object_ptr);
		nm_free(entry);
	}
	objcfg_files = NULL;
	for (entry = objcfg_dirs; entry; entry = next) {
		next = entry->next;
		nm_free(entry->object_ptr);
		nm_free(entry);
	}
	objcfg_dirs = NULL;

	/* free illegal char strings */
	nm_free(illegal_object_chars);
	nm_free(illegal_output_chars);

	/* free file/path variables */
	nm_free(status_file);
	nm_free(debug_file);
	nm_free(log_file);
	mac->x[MACRO_LOGFILE] = NULL; /* assigned from 'log_file' */
	nm_free(temp_file);
	mac->x[MACRO_TEMPFILE] = NULL; /* assigned from temp_file */
	nm_free(temp_path);
	mac->x[MACRO_TEMPPATH] = NULL; /*assigned from temp_path */
	nm_free(check_result_path);
	nm_free(command_file);
	nm_free(qh_socket_path);
	mac->x[MACRO_COMMANDFILE] = NULL; /* assigned from command_file */
	nm_free(log_archive_path);

	for (i = 0; i < MAX_USER_MACROS; i++) {
		nm_free(macro_user[i]);
	}

	/* these have no other reference */
	nm_free(mac->x[MACRO_ADMINEMAIL]);
	nm_free(mac->x[MACRO_ADMINPAGER]);
	nm_free(mac->x[MACRO_RESOURCEFILE]);
	nm_free(mac->x[MACRO_OBJECTCACHEFILE]);
	nm_free(mac->x[MACRO_MAINCONFIGFILE]);
	nm_free(mac->x[MACRO_STATUSDATAFILE]);
	nm_free(mac->x[MACRO_RETENTIONDATAFILE]);
	nm_free(mac->x[MACRO_PROCESSSTARTTIME]);
	nm_free(mac->x[MACRO_EVENTSTARTTIME]);

	if (nm_g_log_handler_id > 0)
		g_log_remove_handler("GLib", nm_g_log_handler_id);

	return;
}

/* reset all system-wide variables, so when we've receive a SIGHUP we can restart cleanly */
int reset_variables(void)
{

	log_file = nm_strdup(get_default_log_file());
	temp_file = nm_strdup(get_default_temp_file());
	temp_path = nm_strdup(get_default_temp_path());
	check_result_path = nm_strdup(get_default_check_result_path());
	command_file = nm_strdup(get_default_command_file());
	qh_socket_path = nm_strdup(get_default_query_socket());
	if (lock_file) /* this is kept across restarts */
		free(lock_file);
	lock_file = nm_strdup(get_default_lock_file());
	log_archive_path = nm_strdup(get_default_log_archive_path());
	debug_file = nm_strdup(get_default_debug_file());

	object_cache_file = nm_strdup(get_default_object_cache_file());
	object_precache_file = nm_strdup(get_default_precached_object_file());

	use_regexp_matches = FALSE;
	use_true_regexp_matching = FALSE;

	use_syslog = DEFAULT_USE_SYSLOG;
	log_service_retries = DEFAULT_LOG_SERVICE_RETRIES;
	log_host_retries = DEFAULT_LOG_HOST_RETRIES;
	log_initial_states = DEFAULT_LOG_INITIAL_STATES;

	enable_notification_suppression_reason_logging = DEFAULT_NSR_LOGGING;
	log_notifications = DEFAULT_NOTIFICATION_LOGGING;
	log_event_handlers = DEFAULT_LOG_EVENT_HANDLERS;
	log_external_commands = DEFAULT_LOG_EXTERNAL_COMMANDS;
	log_passive_checks = DEFAULT_LOG_PASSIVE_CHECKS;

	logging_options = NSLOG_RUNTIME_ERROR | NSLOG_RUNTIME_WARNING | NSLOG_VERIFICATION_ERROR | NSLOG_VERIFICATION_WARNING | NSLOG_CONFIG_ERROR | NSLOG_CONFIG_WARNING | NSLOG_PROCESS_INFO | NSLOG_HOST_NOTIFICATION | NSLOG_SERVICE_NOTIFICATION | NSLOG_EVENT_HANDLER | NSLOG_EXTERNAL_COMMAND | NSLOG_PASSIVE_CHECK | NSLOG_HOST_UP | NSLOG_HOST_DOWN | NSLOG_HOST_UNREACHABLE | NSLOG_SERVICE_OK | NSLOG_SERVICE_WARNING | NSLOG_SERVICE_UNKNOWN | NSLOG_SERVICE_CRITICAL | NSLOG_INFO_MESSAGE;

	syslog_options = NSLOG_RUNTIME_ERROR | NSLOG_RUNTIME_WARNING | NSLOG_VERIFICATION_ERROR | NSLOG_VERIFICATION_WARNING | NSLOG_CONFIG_ERROR | NSLOG_CONFIG_WARNING | NSLOG_PROCESS_INFO | NSLOG_HOST_NOTIFICATION | NSLOG_SERVICE_NOTIFICATION | NSLOG_EVENT_HANDLER | NSLOG_EXTERNAL_COMMAND | NSLOG_PASSIVE_CHECK | NSLOG_HOST_UP | NSLOG_HOST_DOWN | NSLOG_HOST_UNREACHABLE | NSLOG_SERVICE_OK | NSLOG_SERVICE_WARNING | NSLOG_SERVICE_UNKNOWN | NSLOG_SERVICE_CRITICAL | NSLOG_INFO_MESSAGE;

	service_check_timeout = DEFAULT_SERVICE_CHECK_TIMEOUT;
	host_check_timeout = DEFAULT_HOST_CHECK_TIMEOUT;
	event_handler_timeout = DEFAULT_EVENT_HANDLER_TIMEOUT;
	notification_timeout = DEFAULT_NOTIFICATION_TIMEOUT;
	ocsp_timeout = DEFAULT_OCSP_TIMEOUT;
	ochp_timeout = DEFAULT_OCHP_TIMEOUT;

	interval_length = DEFAULT_INTERVAL_LENGTH;

	use_aggressive_host_checking = DEFAULT_AGGRESSIVE_HOST_CHECKING;
	cached_host_check_horizon = DEFAULT_CACHED_HOST_CHECK_HORIZON;
	cached_service_check_horizon = DEFAULT_CACHED_SERVICE_CHECK_HORIZON;
	enable_predictive_host_dependency_checks = DEFAULT_ENABLE_PREDICTIVE_HOST_DEPENDENCY_CHECKS;
	enable_predictive_service_dependency_checks = DEFAULT_ENABLE_PREDICTIVE_SERVICE_DEPENDENCY_CHECKS;

	soft_state_dependencies = FALSE;

	retain_state_information = FALSE;
	retention_update_interval = DEFAULT_RETENTION_UPDATE_INTERVAL;
	use_retained_program_state = TRUE;
	use_retained_scheduling_info = FALSE;
	retention_scheduling_horizon = DEFAULT_RETENTION_SCHEDULING_HORIZON;
	modified_host_process_attributes = MODATTR_NONE;
	modified_service_process_attributes = MODATTR_NONE;
	retained_host_attribute_mask = 0L;
	retained_service_attribute_mask = 0L;
	retained_process_host_attribute_mask = 0L;
	retained_process_service_attribute_mask = 0L;
	retained_contact_host_attribute_mask = 0L;
	retained_contact_service_attribute_mask = 0L;

	check_reaper_interval = DEFAULT_CHECK_REAPER_INTERVAL;
	max_check_reaper_time = DEFAULT_MAX_REAPER_TIME;
	max_check_result_file_age = DEFAULT_MAX_CHECK_RESULT_AGE;
	service_freshness_check_interval = DEFAULT_FRESHNESS_CHECK_INTERVAL;
	host_freshness_check_interval = DEFAULT_FRESHNESS_CHECK_INTERVAL;

	check_external_commands = DEFAULT_CHECK_EXTERNAL_COMMANDS;
	check_orphaned_services = DEFAULT_CHECK_ORPHANED_SERVICES;
	check_orphaned_hosts = DEFAULT_CHECK_ORPHANED_HOSTS;
	check_service_freshness = DEFAULT_CHECK_SERVICE_FRESHNESS;
	check_host_freshness = DEFAULT_CHECK_HOST_FRESHNESS;

	log_rotation_method = LOG_ROTATION_NONE;

	last_log_rotation = 0L;

	max_parallel_service_checks = DEFAULT_MAX_PARALLEL_SERVICE_CHECKS;
	currently_running_service_checks = 0;

	enable_notifications = TRUE;
	execute_service_checks = TRUE;
	accept_passive_service_checks = TRUE;
	execute_host_checks = TRUE;
	accept_passive_service_checks = TRUE;
	enable_event_handlers = TRUE;
	obsess_over_services = FALSE;
	obsess_over_hosts = FALSE;

	next_comment_id = 0L; /* comment and downtime id get initialized to nonzero elsewhere */
	next_downtime_id = 0L;
	next_event_id = 1;

	status_update_interval = DEFAULT_STATUS_UPDATE_INTERVAL;

	event_broker_options = BROKER_NOTHING;

	time_change_threshold = DEFAULT_TIME_CHANGE_THRESHOLD;

	enable_flap_detection = DEFAULT_ENABLE_FLAP_DETECTION;
	low_service_flap_threshold = DEFAULT_LOW_SERVICE_FLAP_THRESHOLD;
	high_service_flap_threshold = DEFAULT_HIGH_SERVICE_FLAP_THRESHOLD;
	low_host_flap_threshold = DEFAULT_LOW_HOST_FLAP_THRESHOLD;
	high_host_flap_threshold = DEFAULT_HIGH_HOST_FLAP_THRESHOLD;

	process_performance_data = DEFAULT_PROCESS_PERFORMANCE_DATA;

	translate_passive_host_checks = DEFAULT_TRANSLATE_PASSIVE_HOST_CHECKS;
	passive_host_checks_are_soft = DEFAULT_PASSIVE_HOST_CHECKS_SOFT;

	additional_freshness_latency = DEFAULT_ADDITIONAL_FRESHNESS_LATENCY;

	debug_level = DEFAULT_DEBUG_LEVEL;
	debug_verbosity = DEFAULT_DEBUG_VERBOSITY;
	max_debug_file_size = DEFAULT_MAX_DEBUG_FILE_SIZE;

	date_format = DATE_FORMAT_US;

	/* initialize macros */
	init_macros();

	global_host_event_handler = NULL;
	global_service_event_handler = NULL;
	global_host_event_handler_ptr = NULL;
	global_service_event_handler_ptr = NULL;

	ocsp_command = NULL;
	ochp_command = NULL;
	ocsp_command_ptr = NULL;
	ochp_command_ptr = NULL;

	/* reset umask */
	umask(S_IWGRP | S_IWOTH);

	return OK;
}

/******************************************************************/
/************************* ESCAPE FUNCTIONS *************************/
/******************************************************************/

/* escapes newlines in a string. */
char *escape_plugin_output(const char *rawbuf)
{
	char *newbuf = NULL;
	int x;
	int y;

	if (rawbuf == NULL)
		return NULL;

	/* count the escapes we need to make. */
	for (x = 0, y = 0; rawbuf[x]; x++) {
		if (rawbuf[x] == '\n')
			y++;
	}

	if (y == 0)
		return strdup(rawbuf);

	if ((newbuf = malloc(x + y + 1)) == NULL)
		return NULL;

	for (x = 0, y = 0; rawbuf[x]; x++) {
		if (rawbuf[x] == '\n') {
			newbuf[y++] = '\\';
			newbuf[y++] = 'n';
		} else
			newbuf[y++] = rawbuf[x];
	}
	newbuf[y] = '\0';

	return newbuf;
}

/* unescapes newlines in a string. */
char *unescape_plugin_output(const char *rawbuf)
{
	char *newbuf = NULL;
	int x;
	int y;

	if (rawbuf == NULL)
		return NULL;

	/* count the replacements we need to make. */
	for (x = 0, y = 0; rawbuf[x]; x++) {
		if (rawbuf[x] == '\\' && rawbuf[x + 1] == 'n')
			x++, y++;
	}

	if (y == 0)
		return nm_strdup(rawbuf);

	if ((newbuf = nm_malloc(x - y + 1)) == NULL)
		return NULL;

	for (x = 0, y = 0; rawbuf[x]; x++) {
		if (rawbuf[x] == '\\' && rawbuf[x + 1] == 'n') {
			x++;
			newbuf[y++] = '\n';
		} else
			newbuf[y++] = rawbuf[x];
	}
	newbuf[y] = '\0';

	return newbuf;
}
