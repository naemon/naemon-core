#include "config.h"
#include "common.h"
#include "objects.h"
#include "statusdata.h"
#include "comments.h"
#include "macros.h"
#include "broker.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "workers.h"
#include "utils.h"
#include "commands.h"
#include "checks.h"
#include "events.h"
#include "logging.h"
#include "defaults.h"
#include "globals.h"
#include "nm_alloc.h"
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
#include "loadctl.h"
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#define SECS_PER_DAY 86400
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

int num_check_workers = 0; /* auto-decide */
char *qh_socket_path = NULL; /* disabled */

char *naemon_user = NULL;
char *naemon_group = NULL;

char *ocsp_command = NULL;
char *ochp_command = NULL;
command *ocsp_command_ptr = NULL;
command *ochp_command_ptr = NULL;
int ocsp_timeout = DEFAULT_OCSP_TIMEOUT;
int ochp_timeout = DEFAULT_OCHP_TIMEOUT;

char *illegal_object_chars = NULL;

int use_regexp_matches;
int use_true_regexp_matching;

int	use_syslog = DEFAULT_USE_SYSLOG;
char *log_file = NULL;
char *log_archive_path = NULL;
int log_notifications = DEFAULT_NOTIFICATION_LOGGING;
int log_service_retries = DEFAULT_LOG_SERVICE_RETRIES;
int log_host_retries = DEFAULT_LOG_HOST_RETRIES;
int log_event_handlers = DEFAULT_LOG_EVENT_HANDLERS;
int log_initial_states = DEFAULT_LOG_INITIAL_STATES;
int log_current_states = DEFAULT_LOG_CURRENT_STATES;
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

struct load_control loadctl;

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
unsigned long next_problem_id = 0L;
unsigned long next_comment_id = 0L;
unsigned long next_notification_id = 0L;

int verify_config = FALSE;
int precache_objects = FALSE;
int use_precached_objects = FALSE;

volatile sig_atomic_t sigshutdown = FALSE;
volatile sig_atomic_t sigrestart = FALSE;
volatile sig_atomic_t sigrotate = FALSE;
volatile sig_atomic_t sigfilesize = FALSE;
volatile sig_atomic_t sig_id = 0;

int daemon_dumps_core = TRUE;

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

int use_large_installation_tweaks = DEFAULT_USE_LARGE_INSTALLATION_TWEAKS;
int enable_environment_macros = TRUE;
int free_child_process_memory = -1;
int child_processes_fork_twice = -1;

char *use_timezone = NULL;

int allow_empty_hostgroup_assignment = DEFAULT_ALLOW_EMPTY_HOSTGROUP_ASSIGNMENT;

/*** perfdata variables ***/
int     perfdata_timeout;
char    *host_perfdata_command = NULL;
char    *service_perfdata_command = NULL;
char    *host_perfdata_file_template = NULL;
char    *service_perfdata_file_template = NULL;
char    *host_perfdata_file = NULL;
char    *service_perfdata_file = NULL;
int     host_perfdata_file_append = TRUE;
int     service_perfdata_file_append = TRUE;
int     host_perfdata_file_pipe = FALSE;
int     service_perfdata_file_pipe = FALSE;
unsigned long host_perfdata_file_processing_interval = 0L;
unsigned long service_perfdata_file_processing_interval = 0L;
char    *host_perfdata_file_processing_command = NULL;
char    *service_perfdata_file_processing_command = NULL;
int     host_perfdata_process_empty_results = DEFAULT_HOST_PERFDATA_PROCESS_EMPTY_RESULTS;
int     service_perfdata_process_empty_results = DEFAULT_SERVICE_PERFDATA_PROCESS_EMPTY_RESULTS;
/*** end perfdata variables */

static long long check_file_size(char *, unsigned long, struct rlimit);

notification    *notification_list;

time_t max_check_result_file_age = DEFAULT_MAX_CHECK_RESULT_AGE;

check_stats     check_statistics[MAX_CHECK_STATS_TYPES];

char *debug_file;
int debug_level = DEFAULT_DEBUG_LEVEL;
int debug_verbosity = DEFAULT_DEBUG_VERBOSITY;
unsigned long   max_debug_file_size = DEFAULT_MAX_DEBUG_FILE_SIZE;

iobroker_set *nagios_iobs = NULL;
squeue_t *nagios_squeue = NULL; /* our scheduling queue */

/* from GNU defines errno as a macro, since it's a per-thread variable */
#ifndef errno
extern int errno;
#endif


static const char *worker_source_name(void *source)
{
	return source ? (const char *)source : "unknown internal source (voodoo, perhaps?)";
}

static const char *spool_file_source_name(void *source)
{
	return "check result spool dir";
}

struct check_engine nagios_check_engine = {
	"Naemon Core",
	worker_source_name,
	NULL,
};

static struct check_engine nagios_spool_check_engine = {
	"Spooled checkresult file",
	spool_file_source_name,
	NULL,
};

const char *check_result_source(check_result *cr)
{
	if (cr->engine)
		return cr->engine->source_name(cr->source);
	return cr->source ? (const char *)cr->source : "(unknown engine)";
}


int set_loadctl_options(char *opts, unsigned int len)
{
	struct kvvec *kvv;
	int i;

	kvv = buf2kvvec(opts, len, '=', ';', 0);
	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv = &kvv->kv[i];

		if (!strcmp(kv->key, "enabled")) {
			if (*kv->value == '1') {
				if (!(loadctl.options & LOADCTL_ENABLED))
					logit(0, 0, "Warning: Enabling experimental load control\n");
				loadctl.options |= LOADCTL_ENABLED;
			} else {
				if (loadctl.options & LOADCTL_ENABLED)
					logit(0, 0, "Warning: Disabling experimental load control\n");
				loadctl.options &= (~LOADCTL_ENABLED);
			}
		} else if (!strcmp(kv->key, "jobs_max")) {
			loadctl.jobs_max = atoi(kv->value);
		} else if (!strcmp(kv->key, "jobs_min")) {
			loadctl.jobs_min = atoi(kv->value);
		} else if (!strcmp(kv->key, "jobs_limit")) {
			loadctl.jobs_limit = atoi(kv->value);
		} else if (!strcmp(kv->key, "check_interval")) {
			loadctl.check_interval = strtoul(kv->value, NULL, 10);
		} else if (!strcmp(kv->key, "backoff_limit")) {
			loadctl.backoff_limit = strtod(kv->value, NULL);
		} else if (!strcmp(kv->key, "rampup_limit")) {
			loadctl.rampup_limit = strtod(kv->value, NULL);
		} else if (!strcmp(kv->key, "backoff_change")) {
			loadctl.backoff_change = atoi(kv->value);
		} else if (!strcmp(kv->key, "rampup_change")) {
			loadctl.rampup_change = atoi(kv->value);
		} else {
			logit(NSLOG_CONFIG_ERROR, TRUE, "Error: Bad loadctl option; %s = %s\n", kv->key, kv->value);
			return 400;
		}
	}

	/* precedence order is "jobs_min -> jobs_max -> jobs_limit" */
	if (loadctl.jobs_max < loadctl.jobs_min)
		loadctl.jobs_max = loadctl.jobs_min;
	if (loadctl.jobs_limit > loadctl.jobs_max)
		loadctl.jobs_limit = loadctl.jobs_max;
	if (loadctl.jobs_limit < loadctl.jobs_min)
		loadctl.jobs_limit = loadctl.jobs_min;
	kvvec_destroy(kvv, 0);
	return 0;
}


/******************************************************************/
/******************** SYSTEM COMMAND FUNCTIONS ********************/
/******************************************************************/

/* executes a system command - used for notifications, event handlers, etc. */
int my_system_r(nagios_macros *mac, char *cmd, int timeout, int *early_timeout, double *exectime, char **output, int max_output_length)
{
	pid_t pid = 0;
	int status = 0;
	int result = 0;
	char buffer[MAX_INPUT_BUFFER] = "";
	int fd[2];
	FILE *fp = NULL;
	int bytes_read = 0;
	struct timeval start_time, end_time;
	dbuf output_dbuf;
	int dbuf_chunk = 1024;
	int flags;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "my_system_r()\n");

	/* initialize return variables */
	if (output != NULL)
		*output = NULL;
	*early_timeout = FALSE;
	*exectime = 0.0;

	/* if no command was passed, return with no error */
	if (cmd == NULL)
		return STATE_OK;

	log_debug_info(DEBUGL_COMMANDS, 1, "Running command '%s'...\n", cmd);

	/* create a pipe */
	if (pipe(fd) < 0) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: pipe() in my_system_r() failed: '%s'\n", strerror(errno));
		return STATE_UNKNOWN;
	}

	/* make the pipe non-blocking */
	fcntl(fd[0], F_SETFL, O_NONBLOCK);
	fcntl(fd[1], F_SETFL, O_NONBLOCK);

	/* get the command start time */
	gettimeofday(&start_time, NULL);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	broker_system_command(NEBTYPE_SYSTEM_COMMAND_START, NEBFLAG_NONE, NEBATTR_NONE, start_time, end_time, *exectime, timeout, *early_timeout, result, cmd, NULL, NULL);
#endif

	/* fork */
	pid = fork();

	/* return an error if we couldn't fork */
	if (pid == -1) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: fork() in my_system_r() failed for command \"%s\": %s\n", cmd, strerror(errno));

		/* close both ends of the pipe */
		close(fd[0]);
		close(fd[1]);

		return STATE_UNKNOWN;
	}

	/* execute the command in the child process */
	if (pid == 0) {

		/* become process group leader */
		setpgid(0, 0);

		/* set environment variables */
		set_all_macro_environment_vars_r(mac, TRUE);

		/* ADDED 11/12/07 EG */
		/* close external command file and shut down worker thread */
		close_command_file();

		/* reset signal handling */
		reset_sighandler();

		/* close pipe for reading */
		close(fd[0]);

		/* prevent fd from being inherited by child processed */
		flags = fcntl(fd[1], F_GETFD, 0);
		flags |= FD_CLOEXEC;
		fcntl(fd[1], F_SETFD, flags);

		/* trap commands that timeout */
		signal(SIGALRM, my_system_sighandler);
		alarm(timeout);

		/* run the command */
		fp = (FILE *)popen(cmd, "r");

		/* report an error if we couldn't run the command */
		if (fp == NULL) {

			strncpy(buffer, "(Error: Could not execute command)\n", sizeof(buffer) - 1);
			buffer[sizeof(buffer) - 1] = '\x0';

			/* write the error back to the parent process */
			uninterrupted_write(fd[1], buffer, strlen(buffer) + 1);

			result = STATE_CRITICAL;
		} else {

			/* write all the lines of output back to the parent process */
			while (fgets(buffer, sizeof(buffer) - 1, fp))
				uninterrupted_write(fd[1], buffer, strlen(buffer));

			/* close the command and get termination status */
			status = pclose(fp);

			/* report an error if we couldn't close the command */
			if (status == -1)
				result = STATE_CRITICAL;
			else {
				if (WEXITSTATUS(status) == 0 && WIFSIGNALED(status))
					result = 128 + WTERMSIG(status);
				result = WEXITSTATUS(status);
			}
		}

		/* close pipe for writing */
		close(fd[1]);

		/* reset the alarm */
		alarm(0);

		/* clear environment variables */
		set_all_macro_environment_vars_r(mac, FALSE);

#ifndef DONT_USE_MEMORY_PERFORMANCE_TWEAKS
		/* free allocated memory */
		/* this needs to be done last, so we don't free memory for variables before they're used above */
		if (free_child_process_memory == TRUE)
			free_memory(mac);
#endif

		_exit(result);
	}

	/* parent waits for child to finish executing command */

	/* close pipe for writing */
	close(fd[1]);

	/* wait for child to exit */
	waitpid(pid, &status, 0);

	/* get the end time for running the command */
	gettimeofday(&end_time, NULL);

	/* return execution time in milliseconds */
	*exectime = (double)((double)(end_time.tv_sec - start_time.tv_sec) + (double)((end_time.tv_usec - start_time.tv_usec) / 1000) / 1000.0);
	if (*exectime < 0.0)
		*exectime = 0.0;

	/* get the exit code returned from the program */
	result = WEXITSTATUS(status);

	/* check for possibly missing scripts/binaries/etc */
	if (result == 126 || result == 127) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Attempting to execute the command \"%s\" resulted in a return code of %d.  Make sure the script or binary you are trying to execute actually exists...\n", cmd, result);
	}

	/* check bounds on the return value */
	if (result < -1 || result > 3)
		result = STATE_UNKNOWN;

	/* initialize dynamic buffer */
	dbuf_init(&output_dbuf, dbuf_chunk);

	/* Opsera patch to check timeout before attempting to read output via pipe. Originally by Sven Nierlein */
	/* if there was a critical return code AND the command time exceeded the timeout thresholds, assume a timeout */
	if (result == STATE_CRITICAL && (end_time.tv_sec - start_time.tv_sec) >= timeout) {

		/* set the early timeout flag */
		*early_timeout = TRUE;

		/* try to kill the command that timed out by sending termination signal to child process group */
		kill((pid_t)(-pid), SIGTERM);
		sleep(1);
		kill((pid_t)(-pid), SIGKILL);
	}

	/* read output if timeout has not occurred */
	else {

		/* initialize output */
		strcpy(buffer, "");

		/* try and read the results from the command output (retry if we encountered a signal) */
		do {
			bytes_read = read(fd[0], buffer, sizeof(buffer) - 1);

			/* append data we just read to dynamic buffer */
			if (bytes_read > 0) {
				buffer[bytes_read] = '\x0';
				dbuf_strcat(&output_dbuf, buffer);
				continue;
			}

			/* handle errors */
			if (bytes_read == -1) {
				/* we encountered a recoverable error, so try again */
				if (errno == EINTR)
					continue;
				/* patch by Henning Brauer to prevent CPU hogging */
				else if (errno == EAGAIN) {
					struct pollfd pfd;

					pfd.fd = fd[0];
					pfd.events = POLLIN;
					poll(&pfd, 1, -1);
					continue;
				} else
					break;
			}

			/* we're done */
			if (bytes_read == 0)
				break;

		} while (1);

		/* cap output length - this isn't necessary, but it keeps runaway plugin output from causing problems */
		if (max_output_length > 0  && (int)output_dbuf.used_size > max_output_length)
			output_dbuf.buf[max_output_length] = '\x0';

		if (output != NULL && output_dbuf.buf)
			*output = nm_strdup(output_dbuf.buf);

	}

	log_debug_info(DEBUGL_COMMANDS, 1, "Execution time=%.3f sec, early timeout=%d, result=%d, output=%s\n", *exectime, *early_timeout, result, (output_dbuf.buf == NULL) ? "(null)" : output_dbuf.buf);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_system_command(NEBTYPE_SYSTEM_COMMAND_END, NEBFLAG_NONE, NEBATTR_NONE, start_time, end_time, *exectime, timeout, *early_timeout, result, cmd, (output_dbuf.buf == NULL) ? NULL : output_dbuf.buf, NULL);
#endif

	/* free memory */
	dbuf_free(&output_dbuf);

	/* close the pipe for reading */
	close(fd[0]);

	return result;
}


/*
 * For API compatibility, we must include a my_system() whose
 * signature doesn't include the nagios_macros variable.
 * NDOUtils uses this. Possibly other modules as well.
 */
int my_system(char *cmd, int timeout, int *early_timeout, double *exectime, char **output, int max_output_length)
{
	return my_system_r(get_global_macros(), cmd, timeout, early_timeout, exectime, output, max_output_length);
}


/* given a "raw" command, return the "expanded" or "whole" command line */
int get_raw_command_line_r(nagios_macros *mac, command *cmd_ptr, char *cmd, char **full_command, int macro_options)
{
	char temp_arg[MAX_COMMAND_BUFFER] = "";
	char *arg_buffer = NULL;
	register int x = 0;
	register int y = 0;
	register int arg_index = 0;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "get_raw_command_line_r()\n");

	/* clear the argv macros */
	clear_argv_macros_r(mac);

	/* make sure we've got all the requirements */
	if (cmd_ptr == NULL || full_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_COMMANDS | DEBUGL_CHECKS | DEBUGL_MACROS, 2, "Raw Command Input: %s\n", cmd_ptr->command_line);

	/* get the full command line */
	*full_command = nm_strdup((cmd_ptr->command_line == NULL) ? "" : cmd_ptr->command_line);

	/* XXX: Crazy indent */
	/* get the command arguments */
	if (cmd != NULL) {

		/* skip the command name (we're about to get the arguments)... */
		for (arg_index = 0;; arg_index++) {
			if (cmd[arg_index] == '!' || cmd[arg_index] == '\x0')
				break;
		}

		/* get each command argument */
		for (x = 0; x < MAX_COMMAND_ARGUMENTS; x++) {

			/* we reached the end of the arguments... */
			if (cmd[arg_index] == '\x0')
				break;

			/* get the next argument */
			/* can't use strtok(), as that's used in process_macros... */
			for (arg_index++, y = 0; y < (int)sizeof(temp_arg) - 1; arg_index++) {

				/* handle escaped argument delimiters */
				if (cmd[arg_index] == '\\' && cmd[arg_index + 1] == '!') {
					arg_index++;
				} else if (cmd[arg_index] == '!' || cmd[arg_index] == '\x0') {
					/* end of argument */
					break;
				}

				/* copy the character */
				temp_arg[y] = cmd[arg_index];
				y++;
			}
			temp_arg[y] = '\x0';

			/* ADDED 01/29/04 EG */
			/* process any macros we find in the argument */
			process_macros_r(mac, temp_arg, &arg_buffer, macro_options);

			mac->argv[x] = arg_buffer;
		}
	}

	log_debug_info(DEBUGL_COMMANDS | DEBUGL_CHECKS | DEBUGL_MACROS, 2, "Expanded Command Output: %s\n", *full_command);

	return OK;
}

/*
 * This function modifies the global macro struct and is thus not
 * threadsafe
 */
int get_raw_command_line(command *cmd_ptr, char *cmd, char **full_command, int macro_options)
{
	nagios_macros *mac;

	mac = get_global_macros();
	return get_raw_command_line_r(mac, cmd_ptr, cmd, full_command, macro_options);
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
/************************* TIME FUNCTIONS *************************/
/******************************************************************/

/* Checks if the given time is in daylight time saving period */
static int is_dst_time(time_t *timestamp)
{
	struct tm *bt = localtime(timestamp);
	return bt->tm_isdst;
}


/* Returns the shift in seconds if the given times are across the daylight time saving period change */
static int get_dst_shift(time_t *start, time_t *end)
{
	int shift = 0, dst_end, dst_start;
	dst_start = is_dst_time(start);
	dst_end = is_dst_time(end);
	if (dst_start < dst_end) {
		shift = 3600;
	} else if (dst_start > dst_end) {
		shift = -3600;
	}
	return shift;
}


/*#define TEST_TIMEPERIODS_A 1*/
static timerange *_get_matching_timerange(time_t test_time, timeperiod *tperiod)
{
	daterange *temp_daterange = NULL;
	time_t start_time = (time_t)0L;
	time_t end_time = (time_t)0L;
	unsigned long days = 0L;
	int year = 0;
	int shift = 0;
	time_t midnight = (time_t)0L;
	struct tm *t, tm_s;
	int daterange_type = 0;
	int test_time_year = 0;
	int test_time_mon = 0;
	int test_time_wday = 0;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "_get_matching_timerange()\n");

	if (tperiod == NULL)
		return NULL;

	t = localtime_r((time_t *)&test_time, &tm_s);
	test_time_year = t->tm_year;
	test_time_mon = t->tm_mon;
	test_time_wday = t->tm_wday;

	/* calculate the start of the day (midnight, 00:00 hours) when the specified test time occurs */
	t->tm_sec = 0;
	t->tm_min = 0;
	t->tm_hour = 0;
	midnight = mktime(t);

	/**** check exceptions first ****/
	for (daterange_type = 0; daterange_type < DATERANGE_TYPES; daterange_type++) {

		for (temp_daterange = tperiod->exceptions[daterange_type]; temp_daterange != NULL; temp_daterange = temp_daterange->next) {

#ifdef TEST_TIMEPERIODS_A
			printf("TYPE: %d\n", daterange_type);
			printf("TEST:     %lu = %s", (unsigned long)test_time, ctime(&test_time));
			printf("MIDNIGHT: %lu = %s", (unsigned long)midnight, ctime(&midnight));
#endif

			/* get the start time */
			switch (daterange_type) {
			case DATERANGE_CALENDAR_DATE:
				t->tm_sec = 0;
				t->tm_min = 0;
				t->tm_hour = 0;
				t->tm_wday = 0;
				t->tm_mday = temp_daterange->smday;
				t->tm_mon = temp_daterange->smon;
				t->tm_year = (temp_daterange->syear - 1900);
				t->tm_isdst = -1;
				start_time = mktime(t);
				break;
			case DATERANGE_MONTH_DATE:
				start_time = calculate_time_from_day_of_month(test_time_year, temp_daterange->smon, temp_daterange->smday);
				break;
			case DATERANGE_MONTH_DAY:
				start_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, temp_daterange->smday);
				break;
			case DATERANGE_MONTH_WEEK_DAY:
				start_time = calculate_time_from_weekday_of_month(test_time_year, temp_daterange->smon, temp_daterange->swday, temp_daterange->swday_offset);
				break;
			case DATERANGE_WEEK_DAY:
				start_time = calculate_time_from_weekday_of_month(test_time_year, test_time_mon, temp_daterange->swday, temp_daterange->swday_offset);
				break;
			default:
				continue;
				break;
			}

			/* get the end time */
			switch (daterange_type) {
			case DATERANGE_CALENDAR_DATE:
				t->tm_sec = 0;
				t->tm_min = 0;
				t->tm_hour = 0;
				t->tm_wday = 0;
				t->tm_mday = temp_daterange->emday;
				t->tm_mon = temp_daterange->emon;
				t->tm_year = (temp_daterange->eyear - 1900);
				t->tm_isdst = -1;
				end_time = mktime(t);
				break;
			case DATERANGE_MONTH_DATE:
				year = test_time_year;
				end_time = calculate_time_from_day_of_month(year, temp_daterange->emon, temp_daterange->emday);
				/* advance a year if necessary: august 2 - february 5 */
				if (end_time < start_time) {
					year++;
					end_time = calculate_time_from_day_of_month(year, temp_daterange->emon, temp_daterange->emday);
				}
				break;
			case DATERANGE_MONTH_DAY:
				end_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, temp_daterange->emday);
				break;
			case DATERANGE_MONTH_WEEK_DAY:
				year = test_time_year;
				end_time = calculate_time_from_weekday_of_month(year, temp_daterange->emon, temp_daterange->ewday, temp_daterange->ewday_offset);
				/* advance a year if necessary: thursday 2 august - monday 3 february */
				if (end_time < start_time) {
					year++;
					end_time = calculate_time_from_weekday_of_month(year, temp_daterange->emon, temp_daterange->ewday, temp_daterange->ewday_offset);
				}
				break;
			case DATERANGE_WEEK_DAY:
				end_time = calculate_time_from_weekday_of_month(test_time_year, test_time_mon, temp_daterange->ewday, temp_daterange->ewday_offset);
				break;
			default:
				continue;
				break;
			}

#ifdef TEST_TIMEPERIODS_A
			printf("START:    %lu = %s", (unsigned long)start_time, ctime(&start_time));
			printf("END:      %lu = %s", (unsigned long)end_time, ctime(&end_time));
#endif

			/* start date was bad, so skip this date range */
			if ((unsigned long)start_time == 0L)
				continue;

			/* end date was bad - see if we can handle the error */
			if ((unsigned long)end_time == 0L) {
				switch (daterange_type) {
				case DATERANGE_CALENDAR_DATE:
					continue;
					break;
				case DATERANGE_MONTH_DATE:
					/* end date can't be helped, so skip it */
					if (temp_daterange->emday < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					/* use same year calculated above */
					end_time = calculate_time_from_day_of_month(year, temp_daterange->emon, -1);
					break;
				case DATERANGE_MONTH_DAY:
					/* end date can't be helped, so skip it */
					if (temp_daterange->emday < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					end_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, -1);
					break;
				case DATERANGE_MONTH_WEEK_DAY:
					/* end date can't be helped, so skip it */
					if (temp_daterange->ewday_offset < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					/* use same year calculated above */
					end_time = calculate_time_from_day_of_month(year, test_time_mon, -1);
					break;
				case DATERANGE_WEEK_DAY:
					/* end date can't be helped, so skip it */
					if (temp_daterange->ewday_offset < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					end_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, -1);
					break;
				default:
					continue;
					break;
				}
			}

			/* calculate skip date start (and end) */
			if (temp_daterange->skip_interval > 1) {

				/* skip start date must be before test time */
				if (start_time > test_time)
					continue;

				/* check if interval is across dlst change and gets the compensation */
				shift = get_dst_shift(&start_time, &midnight);

				/* how many days have passed between skip start date and test time? */
				days = (shift + (unsigned long)midnight - (unsigned long)start_time) / (3600 * 24);

				/* if test date doesn't fall on a skip interval day, bail out early */
				if ((days % temp_daterange->skip_interval) != 0)
					continue;

				/* use midnight of test date as start time */
				else
					start_time = midnight;

				/* if skipping range has no end, use test date as end */
				if ((daterange_type == DATERANGE_CALENDAR_DATE) && (is_daterange_single_day(temp_daterange) == TRUE))
					end_time = midnight;
			}

#ifdef TEST_TIMEPERIODS_A
			printf("NEW START:    %lu = %s", (unsigned long)start_time, ctime(&start_time));
			printf("NEW END:      %lu = %s", (unsigned long)end_time, ctime(&end_time));
			printf("%lu DAYS PASSED\n", days);
			printf("DLST SHIFT:   %i\n", shift);
#endif

			/* time falls inside the range of days
			 * end time < start_time when range covers end-of-$unit
			 * (fe. end-of-month) */

			if (((midnight >= start_time && (midnight <= end_time || start_time > end_time)) || (midnight <= end_time && start_time > end_time))) {
#ifdef TEST_TIMEPERIODS_A
				printf("(MATCH)\n");
#endif
				return temp_daterange->times;
			}
		}
	}

	return tperiod->days[test_time_wday];
}

static int is_time_excluded(time_t when, struct timeperiod *tp)
{
	struct timeperiodexclusion *exc;

	for (exc = tp->exclusions; exc; exc = exc->next) {
		if (check_time_against_period(when, exc->timeperiod_ptr) == OK) {
			return 1;
		}
	}
	return 0;
}

static inline time_t get_midnight(time_t when)
{
	struct tm *t, tm_s;

	t = localtime_r((time_t *)&when, &tm_s);
	t->tm_sec = 0;
	t->tm_min = 0;
	t->tm_hour = 0;
	return mktime(t);
}

static inline int timerange_includes_time(struct timerange *range, time_t when)
{
	return (when >= (time_t)range->range_start && when < (time_t)range->range_end);
}

/* see if the specified time falls into a valid time range in the given time period */
int check_time_against_period(time_t test_time, timeperiod *tperiod)
{
	timerange *temp_timerange = NULL;
	time_t midnight = (time_t)0L;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "check_time_against_period()\n");

	midnight = get_midnight(test_time);

	/* if no period was specified, assume the time is good */
	if (tperiod == NULL)
		return OK;

	if (is_time_excluded(test_time, tperiod))
		return ERROR;

	for (temp_timerange = _get_matching_timerange(test_time, tperiod); temp_timerange != NULL; temp_timerange = temp_timerange->next) {
		if (timerange_includes_time(temp_timerange, test_time - midnight))
			return OK;
	}
	return ERROR;
}


/*#define TEST_TIMEPERIODS_B 1*/
void _get_next_valid_time(time_t pref_time, time_t *valid_time, timeperiod *tperiod);

static void _get_next_invalid_time(time_t pref_time, time_t *invalid_time, timeperiod *tperiod)
{
	timeperiodexclusion *temp_timeperiodexclusion = NULL;
	int depth = 0;
	int max_depth = 300; // commonly roughly equal to "days in the future"
	struct tm *t, tm_s;
	time_t earliest_time = pref_time;
	time_t last_earliest_time = 0;
	time_t midnight = (time_t)0L;
	time_t day_range_start = (time_t)0L;
	time_t day_range_end = (time_t)0L;
	time_t potential_time = 0;
	time_t excluded_time = 0;
	time_t last_range_end = 0;
	int have_earliest_time = FALSE;
	timerange *last_range = NULL, *temp_timerange = NULL;

	/* if no period was specified, assume the time is good */
	if (tperiod == NULL || check_time_against_period(pref_time, tperiod) == ERROR) {
		*invalid_time = pref_time;
		return;
	}

	/* first excluded time may well be the time we're looking for */
	for (temp_timeperiodexclusion = tperiod->exclusions; temp_timeperiodexclusion != NULL; temp_timeperiodexclusion = temp_timeperiodexclusion->next) {
		/* if pref_time is excluded, we're done */
		if (check_time_against_period(pref_time, temp_timeperiodexclusion->timeperiod_ptr) != ERROR) {
			*invalid_time = pref_time;
			return;
		}
		_get_next_valid_time(pref_time, &potential_time, temp_timeperiodexclusion->timeperiod_ptr);
		if (!excluded_time || excluded_time > potential_time)
			excluded_time = potential_time;
	}

	while (earliest_time != last_earliest_time && depth < max_depth) {
		have_earliest_time = FALSE;
		depth++;
		last_earliest_time = earliest_time;

		t = localtime_r((time_t *)&earliest_time, &tm_s);
		t->tm_sec = 0;
		t->tm_min = 0;
		t->tm_hour = 0;
		midnight = mktime(t);

		temp_timerange = _get_matching_timerange(earliest_time, tperiod);

		for (; temp_timerange; last_range = temp_timerange, temp_timerange = temp_timerange->next) {
			/* ranges with start/end of zero mean exlude this day */

			day_range_start = (time_t)(midnight + temp_timerange->range_start);
			day_range_end = (time_t)(midnight + temp_timerange->range_end);

#ifdef TEST_TIMEPERIODS_B
			printf("  INVALID RANGE START: %lu (%lu) = %s", temp_timerange->range_start, (unsigned long)day_range_start, ctime(&day_range_start));
			printf("  INVALID RANGE END:   %lu (%lu) = %s", temp_timerange->range_end, (unsigned long)day_range_end, ctime(&day_range_end));
#endif

			if (temp_timerange->range_start == 0 && temp_timerange->range_end == 0)
				continue;

			if (excluded_time && day_range_end > excluded_time) {
				earliest_time = excluded_time;
				have_earliest_time = TRUE;
				break;
			}

			/*
			 * Unless two consecutive days have adjoining timeranges,
			 * the end of the last period is the start of the first
			 * invalid time. This only needs special-casing when the
			 * last range of the previous day ends at midnight, and
			 * also catches the special case when there are only
			 * exceptions in a timeperiod and some days are skipped
			 * entirely.
			 */
			if (last_range && last_range->range_end == SECS_PER_DAY && last_range_end && day_range_start != last_range_end) {
				earliest_time = last_range_end;
				have_earliest_time = TRUE;
				break;
			}

			/* stash this day_range_end in case we skip a day */
			last_range_end = day_range_end;

			if (pref_time <= day_range_end && temp_timerange->range_end != SECS_PER_DAY) {
				earliest_time = day_range_end;
				have_earliest_time = TRUE;
#ifdef TEST_TIMEPERIODS_B
				printf("    EARLIEST INVALID TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif
				break;
			}
		}

		/* if we found this in the exclusions, we're done */
		if (have_earliest_time == TRUE) {
			break;
		}

		earliest_time = midnight + SECS_PER_DAY;
	}
#ifdef TEST_TIMEPERIODS_B
	printf("    FINAL EARLIEST INVALID TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif

	if (depth == max_depth)
		*invalid_time = pref_time;
	else
		*invalid_time = earliest_time;
}


/* Separate this out from public get_next_valid_time for testing */
void _get_next_valid_time(time_t pref_time, time_t *valid_time, timeperiod *tperiod)
{
	timeperiodexclusion *temp_timeperiodexclusion = NULL;
	int depth = 0;
	int max_depth = 300; // commonly roughly equal to "days in the future"
	time_t earliest_time = pref_time;
	time_t last_earliest_time = 0;
	struct tm *t, tm_s;
	time_t midnight = (time_t)0L;
	time_t day_range_start = (time_t)0L;
	time_t day_range_end = (time_t)0L;
	int have_earliest_time = FALSE;
	timerange *temp_timerange = NULL;

	/* if no period was specified, assume the time is good */
	if (tperiod == NULL) {
		*valid_time = pref_time;
		return;
	}

	while (earliest_time != last_earliest_time && depth < max_depth) {
		time_t potential_time = pref_time;
		have_earliest_time = FALSE;
		depth++;
		last_earliest_time = earliest_time;

		t = localtime_r((time_t *)&earliest_time, &tm_s);
		t->tm_sec = 0;
		t->tm_min = 0;
		t->tm_hour = 0;
		midnight = mktime(t);

		temp_timerange = _get_matching_timerange(earliest_time, tperiod);
#ifdef TEST_TIMEPERIODS_B
		printf("  RANGE START: %lu\n", temp_timerange ? temp_timerange->range_start : 0);
		printf("  RANGE END:   %lu\n", temp_timerange ? temp_timerange->range_end : 0);
#endif

		for (; temp_timerange != NULL; temp_timerange = temp_timerange->next) {
			depth++;
			/* ranges with start/end of zero mean exlude this day */
			if (temp_timerange->range_start == 0 && temp_timerange->range_end == 0) {
				continue;
			}

			day_range_start = (time_t)(midnight + temp_timerange->range_start);
			day_range_end = (time_t)(midnight + temp_timerange->range_end);

#ifdef TEST_TIMEPERIODS_B
			printf("  RANGE START: %lu (%lu) = %s", temp_timerange->range_start, (unsigned long)day_range_start, ctime(&day_range_start));
			printf("  RANGE END:   %lu (%lu) = %s", temp_timerange->range_end, (unsigned long)day_range_end, ctime(&day_range_end));
#endif

			/* range is out of bounds */
			if (day_range_end <= last_earliest_time) {
				continue;
			}

			/* preferred time occurs before range start, so use range start time as earliest potential time */
			if (day_range_start >= last_earliest_time)
				potential_time = day_range_start;
			/* preferred time occurs between range start/end, so use preferred time as earliest potential time */
			else if (day_range_end >= last_earliest_time)
				potential_time = last_earliest_time;

			/* is this the earliest time found thus far? */
			if (have_earliest_time == FALSE || potential_time < earliest_time) {
				earliest_time = potential_time;
				have_earliest_time = TRUE;
#ifdef TEST_TIMEPERIODS_B
				printf("    EARLIEST TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif
			}
		}

		if (have_earliest_time == FALSE) {
			earliest_time = midnight + SECS_PER_DAY;
		} else {
			time_t max_excluded = 0, excluded_time = 0;
			for (temp_timeperiodexclusion = tperiod->exclusions; temp_timeperiodexclusion != NULL; temp_timeperiodexclusion = temp_timeperiodexclusion->next) {
				if (check_time_against_period(earliest_time, temp_timeperiodexclusion->timeperiod_ptr) == ERROR) {
					continue;
				}
				_get_next_invalid_time(earliest_time, &excluded_time, temp_timeperiodexclusion->timeperiod_ptr);
				if (!max_excluded || max_excluded < excluded_time) {
					max_excluded = excluded_time;
					earliest_time = excluded_time;
				}
			}
			if (max_excluded)
				earliest_time = max_excluded;
#ifdef TEST_TIMEPERIODS_B
			printf("    FINAL EARLIEST TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif
		}
	}

	if (depth == max_depth)
		*valid_time = pref_time;
	else
		*valid_time = earliest_time;
}


/* given a preferred time, get the next valid time within a time period */
void get_next_valid_time(time_t pref_time, time_t *valid_time, timeperiod *tperiod)
{
	time_t current_time = (time_t)0L;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "get_next_valid_time()\n");

	/* get time right now, preferred time must be now or in the future */
	time(&current_time);

	pref_time = (pref_time < current_time) ? current_time : pref_time;

	_get_next_valid_time(pref_time, valid_time, tperiod);
}


/* tests if a date range covers just a single day */
int is_daterange_single_day(daterange *dr)
{

	if (dr == NULL)
		return FALSE;

	if (dr->syear != dr->eyear)
		return FALSE;
	if (dr->smon != dr->emon)
		return FALSE;
	if (dr->smday != dr->emday)
		return FALSE;
	if (dr->swday != dr->ewday)
		return FALSE;
	if (dr->swday_offset != dr->ewday_offset)
		return FALSE;

	return TRUE;
}


/* returns a time (midnight) of particular (3rd, last) day in a given month */
time_t calculate_time_from_day_of_month(int year, int month, int monthday)
{
	time_t midnight;
	int day = 0;
	struct tm t;

#ifdef TEST_TIMEPERIODS
	printf("YEAR: %d, MON: %d, MDAY: %d\n", year, month, monthday);
#endif

	/* positive day (3rd day) */
	if (monthday > 0) {

		t.tm_sec = 0;
		t.tm_min = 0;
		t.tm_hour = 0;
		t.tm_year = year;
		t.tm_mon = month;
		t.tm_mday = monthday;
		t.tm_isdst = -1;

		midnight = mktime(&t);

#ifdef TEST_TIMEPERIODS
		printf("MIDNIGHT CALC: %s", ctime(&midnight));
#endif

		/* if we rolled over to the next month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	/* negative offset (last day, 3rd to last day) */
	else {
		/* find last day in the month */
		day = 32;
		do {
			/* back up a day */
			day--;

			/* make the new time */
			t.tm_mon = month;
			t.tm_year = year;
			t.tm_mday = day;
			t.tm_isdst = -1;
			midnight = mktime(&t);

		} while (t.tm_mon != month);

		/* now that we know the last day, back up more */
		/* make the new time */
		t.tm_mon = month;
		t.tm_year = year;
		/* -1 means last day of month, so add one to to make this correct - Mike Bird */
		t.tm_mday += (monthday < -30) ? -30 : monthday + 1;
		t.tm_isdst = -1;
		midnight = mktime(&t);

		/* if we rolled over to the previous month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	return midnight;
}


/* returns a time (midnight) of particular (3rd, last) weekday in a given month */
time_t calculate_time_from_weekday_of_month(int year, int month, int weekday, int weekday_offset)
{
	time_t midnight;
	int days = 0;
	int weeks = 0;
	struct tm t;

	t.tm_sec = 0;
	t.tm_min = 0;
	t.tm_hour = 0;
	t.tm_year = year;
	t.tm_mon = month;
	t.tm_mday = 1;
	t.tm_isdst = -1;

	midnight = mktime(&t);

	/* how many days must we advance to reach the first instance of the weekday this month? */
	days = weekday - (t.tm_wday);
	if (days < 0)
		days += 7;

	/* positive offset (3rd thursday) */
	if (weekday_offset > 0) {

		/* how many weeks must we advance (no more than 5 possible) */
		weeks = (weekday_offset > 5) ? 5 : weekday_offset;
		days += ((weeks - 1) * 7);

		/* make the new time */
		t.tm_mon = month;
		t.tm_year = year;
		t.tm_mday = days + 1;
		t.tm_isdst = -1;
		midnight = mktime(&t);

		/* if we rolled over to the next month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	/* negative offset (last thursday, 3rd to last tuesday) */
	else {
		/* find last instance of weekday in the month */
		days += (5 * 7);
		do {
			/* back up a week */
			days -= 7;

			/* make the new time */
			t.tm_mon = month;
			t.tm_year = year;
			t.tm_mday = days + 1;
			t.tm_isdst = -1;
			midnight = mktime(&t);

		} while (t.tm_mon != month);

		/* now that we know the last instance of the weekday, back up more */
		weeks = (weekday_offset < -5) ? -5 : weekday_offset;
		days = ((weeks + 1) * 7);

		/* make the new time */
		t.tm_mon = month;
		t.tm_year = year;
		t.tm_mday += days;
		t.tm_isdst = -1;
		midnight = mktime(&t);

		/* if we rolled over to the previous month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	return midnight;
}


/******************************************************************/
/******************** SIGNAL HANDLER FUNCTIONS ********************/
/******************************************************************/

/* trap signals so we can exit gracefully */
void setup_sighandler(void)
{
	/* remove buffering from stderr, stdin, and stdout */
	setbuf(stdin, (char *)NULL);
	setbuf(stdout, (char *)NULL);
	setbuf(stderr, (char *)NULL);

	/* initialize signal handling */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGQUIT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGUSR1, sighandler);

	return;
}


/* reset signal handling... */
void reset_sighandler(void)
{

	/* set signal handling to default actions */
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGXFSZ, SIG_DFL);
	signal(SIGUSR1, SIG_DFL);

	return;
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
	case SIGPIPE: sigshutdown = TRUE; break;
	}


}

void signal_react() {
	int signum = sig_id;
	if (signum <= 0)
		return;

	if (sigrestart) {
		/* we received a SIGHUP, so restart... */
		logit(NSLOG_PROCESS_INFO, TRUE, "Caught '%s', restarting...\n", strsignal(signum));
	} else if (sigfilesize) {
		handle_sigxfsz();
	} else if (sigshutdown) {
		/* else begin shutting down... */
		logit(NSLOG_PROCESS_INFO, TRUE, "Caught '%s', shutting down...\n", strsignal(signum));
	}
	sig_id = 0;
}


/* handle timeouts when executing commands via my_system_r() */
void my_system_sighandler(int sig)
{
	/* force the child process to exit... */
	_exit(STATE_CRITICAL);
}

/**
 * Write all of nbyte bytes of buf to fd, and don't let EINTR/EAGAIN stop you.
 * Returns 0 on success. On error, returns -1 and errno is set to indicate the
 * error
 */
int uninterrupted_write(int fd, const void *buf, size_t nbyte)
{
	size_t c = 0;
	int ret = 0;
	while ( c < nbyte ) {
		ret = write(fd, (char *) buf + c, nbyte - c);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			logit(NSLOG_RUNTIME_ERROR, TRUE, "%s", strerror(errno));
			return -1;
		}
		c += (size_t)ret;
	}
	return 0;
}

/**
 * Handle the SIGXFSZ signal. A SIGXFSZ signal is received when a file exceeds
 * the maximum allowable size either as dictated by the fzise paramater in
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
		logit(NSLOG_RUNTIME_ERROR, TRUE,
		      "Unable to determine current resoure limits: %s\n",
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
		logit(NSLOG_RUNTIME_ERROR, TRUE, "SIGXFSZ received because a "
		      "file's size may have exceeded the file size limits of "
		      "the filesystem. The largest file checked, '%s', has a "
		      "size of %lld bytes", max_name, max_size);

	} else {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "SIGXFSZ received but unable to "
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
		logit(NSLOG_RUNTIME_ERROR, TRUE,
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
	logit(NSLOG_RUNTIME_ERROR, TRUE, "Size of file '%s' (%llu) "
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
static void set_working_directory(void) {
	if (chdir("/") != 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Aborting. Failed to set daemon working directory (/): %s\n", strerror(errno));
		cleanup();
		exit(ERROR);
	}
}

int daemon_init(void)
{
	pid_t pid = -1;
	int pidno = 0;
	int lockfile = 0;
	int val = 0;
	char buf[256];
	struct flock lock;
	struct rlimit limit;
	set_working_directory();
	umask(S_IWGRP | S_IWOTH);

	/* close existing stdin, stdout, stderr */
	close(0);
	close(1);
	close(2);

	/* THIS HAS TO BE DONE TO AVOID PROBLEMS WITH STDERR BEING REDIRECTED TO SERVICE MESSAGE PIPE! */
	/* re-open stdin, stdout, stderr with known values */
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_WRONLY);

	lockfile = open(lock_file, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

	if (lockfile < 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Failed to obtain lock on file %s: %s\n", lock_file, strerror(errno));
		logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "Bailing out due to errors encountered while attempting to daemonize... (PID=%d)", (int)getpid());

		cleanup();
		exit(ERROR);
	}

	/* see if we can read the contents of the lockfile */
	if ((val = read(lockfile, buf, (size_t)10)) < 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Lockfile exists but cannot be read");
		cleanup();
		exit(ERROR);
	}

	/* we read something - check the PID */
	if (val > 0) {
		if ((val = sscanf(buf, "%d", &pidno)) < 1) {
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Lockfile '%s' does not contain a valid PID (%s)", lock_file, buf);
			cleanup();
			exit(ERROR);
		}
	}

	/* check for SIGHUP */
	if (val == 1 && (pid = (pid_t)pidno) == getpid()) {
		close(lockfile);
		return OK;
	}

	/* exit on errors... */
	if ((pid = fork()) < 0)
		return (ERROR);

	/* parent process goes away.. */
	else if ((int)pid != 0)
		exit(OK);

	/* child continues... */

	/* child becomes session leader... */
	setsid();

	/* place a file lock on the lock file */
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;
	if (fcntl(lockfile, F_SETLK, &lock) < 0) {
		if (errno == EACCES || errno == EAGAIN) {
			fcntl(lockfile, F_GETLK, &lock);
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Lockfile '%s' looks like its already held by another instance of Naemon (PID %d).  Bailing out...", lock_file, (int)lock.l_pid);
		} else
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Cannot lock lockfile '%s': %s. Bailing out...", lock_file, strerror(errno));

		cleanup();
		exit(ERROR);
	}

	/* prevent daemon from dumping a core file... */
	if (daemon_dumps_core == FALSE) {
		getrlimit(RLIMIT_CORE, &limit);
		limit.rlim_cur = 0;
		setrlimit(RLIMIT_CORE, &limit);
	}

	/* write PID to lockfile... */
	lseek(lockfile, 0, SEEK_SET);
	if (ftruncate(lockfile, 0) != 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Cannot truncate lockfile '%s': %s. Bailing out...", lock_file, strerror(errno));
		cleanup();
		exit(ERROR);
	}
	sprintf(buf, "%d\n", (int)getpid());
	uninterrupted_write(lockfile, buf, strlen(buf));

	/* make sure lock file stays open while program is executing... */
	val = fcntl(lockfile, F_GETFD, 0);
	val |= FD_CLOEXEC;
	fcntl(lockfile, F_SETFD, val);

#ifdef USE_EVENT_BROKER
	/* send program data to broker */
	broker_program_state(NEBTYPE_PROCESS_DAEMONIZE, NEBFLAG_NONE, NEBATTR_NONE, NULL);
#endif

	return OK;
}


/******************************************************************/
/*********************** SECURITY FUNCTIONS ***********************/
/******************************************************************/

/* drops privileges */
int drop_privileges(char *user, char *group)
{
	uid_t uid = -1;
	gid_t gid = -1;
	struct group *grp = NULL;
	struct passwd *pw = NULL;
	int result = OK;

	/* only drop privileges if we're running as root, so we don't interfere with being debugged while running as some random user */
	if (getuid() != 0)
		return OK;

	/* set effective group ID */
	if (group != NULL) {

		/* see if this is a group name */
		if (strspn(group, "0123456789") < strlen(group)) {
			grp = (struct group *)getgrnam(group);
			if (grp != NULL)
				gid = (gid_t)(grp->gr_gid);
			else
				logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Could not get group entry for '%s'", group);
		}

		/* else we were passed the GID */
		else
			gid = (gid_t)atoi(group);
	}

	/* set effective user ID */
	if (user != NULL) {

		/* see if this is a user name */
		if (strspn(user, "0123456789") < strlen(user)) {
			pw = (struct passwd *)getpwnam(user);
			if (pw != NULL)
				uid = (uid_t)(pw->pw_uid);
			else
				logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Could not get passwd entry for '%s'", user);
		}

		/* else we were passed the UID */
		else
			uid = (uid_t)atoi(user);
	}

	/* now that we know what to change to, we fix log file permissions */
	fix_log_file_owner(uid, gid);

	/* set effective group ID if other than current EGID */
	if (gid != getegid()) {
		if (setgid(gid) == -1) {
			logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Could not set effective GID=%d", (int)gid);
			result = ERROR;
		}
	}
#ifdef HAVE_INITGROUPS

	if (uid != geteuid()) {

		/* initialize supplementary groups */
		if (initgroups(user, gid) == -1) {
			if (errno == EPERM)
				logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Unable to change supplementary groups using initgroups() -- I hope you know what you're doing");
			else {
				logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Possibly root user failed dropping privileges with initgroups()");
				return ERROR;
			}
		}
	}
#endif
	if (setuid(uid) == -1) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Could not set effective UID=%d", (int)uid);
		result = ERROR;
	}

#ifdef HAVE_SYS_PRCTL_H
	if (daemon_dumps_core)
		prctl(PR_SET_DUMPABLE, 1);
#endif

	return result;
}


/******************************************************************/
/************************* IPC FUNCTIONS **************************/
/******************************************************************/

/* processes files in the check result queue directory */
int process_check_result_queue(char *dirname)
{
	char file[MAX_FILENAME_LENGTH];
	DIR *dirp = NULL;
	struct dirent *dirfile = NULL;
	register int x = 0;
	struct stat stat_buf;
	struct stat ok_stat_buf;
	char *temp_buffer = NULL;
	int result = OK, check_result_files = 0;
	time_t start;

	/* make sure we have what we need */
	if (dirname == NULL) {
		logit(NSLOG_CONFIG_ERROR, TRUE, "Error: No check result queue directory specified.\n");
		return ERROR;
	}

	/* open the directory for reading */
	if ((dirp = opendir(dirname)) == NULL) {
		logit(NSLOG_CONFIG_ERROR, TRUE, "Error: Could not open check result queue directory '%s' for reading.\n", dirname);
		return ERROR;
	}

	log_debug_info(DEBUGL_CHECKS, 1, "Starting to read check result queue '%s'...\n", dirname);

	start = time(NULL);

	/* process all files in the directory... */
	while ((dirfile = readdir(dirp)) != NULL) {
		/* bail out if we encountered a signal */
		if (sigshutdown == TRUE || sigrestart == TRUE) {
			log_debug_info(DEBUGL_CHECKS, 0, "Breaking out of check result reaper: signal encountered\n");
			break;
		}

		/* break out if we've been here too long */
		if (start + max_check_reaper_time < time(NULL)) {
			log_debug_info(DEBUGL_CHECKS, 0, "Breaking out of check result reaper: max time (%ds) exceeded\n", max_check_reaper_time);
			break;
		}

		/* create /path/to/file */
		snprintf(file, sizeof(file), "%s/%s", dirname, dirfile->d_name);
		file[sizeof(file) - 1] = '\x0';

		/* process this if it's a check result file... */
		x = strlen(dirfile->d_name);
		if (x == 7 && dirfile->d_name[0] == 'c') {

			if (stat(file, &stat_buf) == -1) {
				logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Could not stat() check result file '%s'.\n", file);
				continue;
			}

			/* we only care about real files */
			if (!S_ISREG(stat_buf.st_mode))
				continue;

			/* at this point we have a regular file... */

			/* if the file is too old, we delete it */
			if (stat_buf.st_mtime + max_check_result_file_age < time(NULL)) {
				delete_check_result_file(dirfile->d_name);
				continue;
			}

			/* can we find the associated ok-to-go file ? */
			nm_asprintf(&temp_buffer, "%s.ok", file);
			result = stat(temp_buffer, &ok_stat_buf);
			my_free(temp_buffer);
			if (result == -1)
				continue;

			/* process the file */
			result = process_check_result_file(file);

			/* break out if we encountered an error */
			if (result == ERROR)
				break;

			check_result_files++;
		}
	}

	closedir(dirp);

	return check_result_files;

}


int process_check_result(check_result *cr)
{
	const char *source_name;
	if (!cr)
		return ERROR;

	source_name = check_result_source(cr);

	if (cr->object_check_type == SERVICE_CHECK) {
		service *svc;
		svc = find_service(cr->host_name, cr->service_description);
		if (!svc) {
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Got check result for service '%s' on host '%s'. Unable to find service\n",
			      cr->service_description, cr->host_name);
			return ERROR;
		}
		log_debug_info(DEBUGL_CHECKS, 2, "Processing check result for service '%s' on host '%s'\n",
		               svc->description, svc->host_name);
		svc->check_source = source_name;
		return handle_async_service_check_result(svc, cr);
	}
	if (cr->object_check_type == HOST_CHECK) {
		host *hst;
		hst = find_host(cr->host_name);
		if (!hst) {
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Got host checkresult for '%s', but no such host can be found\n", cr->host_name);
			return ERROR;
		}
		log_debug_info(DEBUGL_CHECKS, 2, "Processing check result for host '%s'\n", hst->name);
		hst->check_source = source_name;
		return handle_async_host_check_result(hst, cr);
	}

	/* We should never end up here */
	logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Unknown object check type for checkresult: %d; (host_name: %s; service_description: %s)\n",
	      cr->object_check_type,
	      cr->host_name ? cr->host_name : "(null)",
	      cr->service_description ? cr->service_description : "(null)");

	return ERROR;
}


/* reads check result(s) from a file */
int process_check_result_file(char *fname)
{
	mmapfile *thefile = NULL;
	char *input = NULL;
	char *var = NULL;
	char *val = NULL;
	char *v1 = NULL, *v2 = NULL;
	time_t current_time;
	check_result cr;

	if (fname == NULL)
		return ERROR;

	init_check_result(&cr);
	cr.engine = &nagios_spool_check_engine;

	time(&current_time);

	log_debug_info(DEBUGL_CHECKS, 1, "Processing check result file: '%s'\n", fname);

	/* open the file for reading */
	if ((thefile = mmap_fopen(fname)) == NULL) {

		/* try removing the file - zero length files can't be mmap()'ed, so it might exist */
		unlink(fname);

		return ERROR;
	}

	/* read in all lines from the file */
	while (1) {

		/* free memory */
		my_free(input);

		/* read the next line */
		if ((input = mmap_fgets_multiline(thefile)) == NULL)
			break;

		/* skip comments */
		if (input[0] == '#')
			continue;

		/* empty line indicates end of record */
		else if (input[0] == '\n') {

			/* do we have the minimum amount of data? */
			if (cr.host_name != NULL && cr.output != NULL) {

				/* process the check result */
				process_check_result(&cr);

			}

			/* cleanse for next check result */
			free_check_result(&cr);
			init_check_result(&cr);
			cr.output_file = fname;
		}

		if ((var = my_strtok(input, "=")) == NULL)
			continue;
		if ((val = my_strtok(NULL, "\n")) == NULL)
			continue;

		/* found the file time */
		if (!strcmp(var, "file_time")) {

			/* file is too old - ignore check results it contains and delete it */
			/* this will only work as intended if file_time comes before check results */
			if (max_check_result_file_age > 0 && (current_time - (time_t)(strtoul(val, NULL, 0)) > max_check_result_file_age)) {
				break;
			}
		}

		/* else we have check result data */
		else {
			if (!strcmp(var, "host_name"))
				cr.host_name = nm_strdup(val);
			else if (!strcmp(var, "service_description")) {
				cr.service_description = nm_strdup(val);
				cr.object_check_type = SERVICE_CHECK;
			} else if (!strcmp(var, "check_type"))
				cr.check_type = atoi(val);
			else if (!strcmp(var, "check_options"))
				cr.check_options = atoi(val);
			else if (!strcmp(var, "scheduled_check"))
				cr.scheduled_check = atoi(val);
			else if (!strcmp(var, "reschedule_check"))
				cr.reschedule_check = atoi(val);
			else if (!strcmp(var, "latency"))
				cr.latency = strtod(val, NULL);
			else if (!strcmp(var, "start_time")) {
				if ((v1 = strtok(val, ".")) == NULL)
					continue;
				if ((v2 = strtok(NULL, "\n")) == NULL)
					continue;
				cr.start_time.tv_sec = strtoul(v1, NULL, 0);
				cr.start_time.tv_usec = strtoul(v2, NULL, 0);
			} else if (!strcmp(var, "finish_time")) {
				if ((v1 = strtok(val, ".")) == NULL)
					continue;
				if ((v2 = strtok(NULL, "\n")) == NULL)
					continue;
				cr.finish_time.tv_sec = strtoul(v1, NULL, 0);
				cr.finish_time.tv_usec = strtoul(v2, NULL, 0);
			} else if (!strcmp(var, "early_timeout"))
				cr.early_timeout = atoi(val);
			else if (!strcmp(var, "exited_ok"))
				cr.exited_ok = atoi(val);
			else if (!strcmp(var, "return_code"))
				cr.return_code = atoi(val);
			else if (!strcmp(var, "output"))
				cr.output = nm_strdup(val);
		}
	}

	/* do we have the minimum amount of data? */
	if (cr.host_name != NULL && cr.output != NULL) {

		/* process check result */
		process_check_result(&cr);
	}

	free_check_result(&cr);

	/* free memory and close file */
	my_free(input);
	mmap_fclose(thefile);

	/* delete the file (as well its ok-to-go file) */
	delete_check_result_file(fname);

	return OK;
}


/* deletes as check result file, as well as its ok-to-go file */
int delete_check_result_file(char *fname)
{
	char *temp_buffer = NULL;

	/* delete the result file */
	unlink(fname);

	/* delete the ok-to-go file */
	nm_asprintf(&temp_buffer, "%s.ok", fname);
	unlink(temp_buffer);
	my_free(temp_buffer);

	return OK;
}


/* initializes a host/service check result */
int init_check_result(check_result *info)
{

	if (info == NULL)
		return ERROR;

	/* reset vars */
	info->object_check_type = HOST_CHECK;
	info->host_name = NULL;
	info->service_description = NULL;
	info->check_type = CHECK_TYPE_ACTIVE;
	info->check_options = CHECK_OPTION_NONE;
	info->scheduled_check = FALSE;
	info->reschedule_check = FALSE;
	info->output_file_fp = NULL;
	info->latency = 0.0;
	info->start_time.tv_sec = 0;
	info->start_time.tv_usec = 0;
	info->finish_time.tv_sec = 0;
	info->finish_time.tv_usec = 0;
	info->early_timeout = FALSE;
	info->exited_ok = TRUE;
	info->return_code = 0;
	info->output = NULL;
	info->source = NULL;
	info->engine = NULL;

	return OK;
}


/* frees memory associated with a host/service check result */
int free_check_result(check_result *info)
{

	if (info == NULL)
		return OK;

	my_free(info->host_name);
	my_free(info->service_description);
	my_free(info->output);

	return OK;
}


/******************************************************************/
/************************ STRING FUNCTIONS ************************/
/******************************************************************/

/* gets the next string from a buffer in memory - strings are terminated by newlines, which are removed */
char *get_next_string_from_buf(char *buf, int *start_index, int bufsize)
{
	char *sptr = NULL;
	const char *nl = "\n";
	int x;

	if (buf == NULL || start_index == NULL)
		return NULL;
	if (bufsize < 0)
		return NULL;
	if (*start_index >= (bufsize - 1))
		return NULL;

	sptr = buf + *start_index;

	/* end of buffer */
	if (sptr[0] == '\x0')
		return NULL;

	x = strcspn(sptr, nl);
	sptr[x] = '\x0';

	*start_index += x + 1;

	return sptr;
}


/* determines whether or not an object name (host, service, etc) contains illegal characters */
int contains_illegal_object_chars(char *name)
{
	register int x = 0;
	register int y = 0;

	if (name == NULL || illegal_object_chars == NULL)
		return FALSE;

	x = (int)strlen(name) - 1;

	for (; x >= 0; x--) {
		/* illegal user-specified characters */
		if (illegal_object_chars != NULL)
			for (y = 0; illegal_object_chars[y]; y++)
				if (name[x] == illegal_object_chars[y])
					return TRUE;
	}

	return FALSE;
}


/* escapes newlines in a string */
char *escape_newlines(char *rawbuf)
{
	char *newbuf = NULL;
	register int x, y;

	if (rawbuf == NULL)
		return NULL;

	/* allocate enough memory to escape all chars if necessary */
	newbuf = nm_malloc((strlen(rawbuf) * 2) + 1);
	for (x = 0, y = 0; rawbuf[x] != (char)'\x0'; x++) {

		/* escape backslashes */
		if (rawbuf[x] == '\\') {
			newbuf[y++] = '\\';
			newbuf[y++] = '\\';
		}

		/* escape newlines */
		else if (rawbuf[x] == '\n') {
			newbuf[y++] = '\\';
			newbuf[y++] = 'n';
		}

		else
			newbuf[y++] = rawbuf[x];
	}
	newbuf[y] = '\x0';

	return newbuf;
}


/* compares strings */
int compare_strings(char *val1a, char *val2a)
{

	/* use the compare_hashdata() function */
	return compare_hashdata(val1a, NULL, val2a, NULL);
}


/******************************************************************/
/************************* FILE FUNCTIONS *************************/
/******************************************************************/

/* renames a file - works across filesystems (Mike Wiacek) */
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

		/* an error occurred because the source and dest files are on different filesystems */
		if (errno == EXDEV) {

			/* try copying the file */
			if (my_fcopy(source, dest) == ERROR) {
				logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Unable to rename file '%s' to '%s': %s\n", source, dest, strerror(errno));
				return -1;
			}

			/* delete the original file */
			unlink(source);

			/* reset result since we successfully copied file */
			rename_result = 0;
		}

		/* some other error occurred */
		else {
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Unable to rename file '%s' to '%s': %s\n", source, dest, strerror(errno));
			return rename_result;
		}
	}

	return rename_result;
}


/*
 * copy a file from the path at source to the already opened
 * destination file dest.
 * This is handy when creating tempfiles with mkstemp()
 */
int my_fdcopy(char *source, char *dest, int dest_fd)
{
	int source_fd, rd_result = 0, wr_result = 0;
	int tot_written = 0, tot_read = 0, buf_size = 0;
	struct stat st;
	char *buf;

	/* open source file for reading */
	if ((source_fd = open(source, O_RDONLY, 0644)) < 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Unable to open file '%s' for reading: %s\n", source, strerror(errno));
		return ERROR;
	}

	/*
	 * find out how large the source-file is so we can be sure
	 * we've written all of it
	 */
	if (fstat(source_fd, &st) < 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Unable to stat source file '%s' for my_fcopy(): %s\n", source, strerror(errno));
		close(source_fd);
		return ERROR;
	}

	/*
	 * If the file is huge, read it and write it in chunks.
	 * This value (128K) is the result of "pick-one-at-random"
	 * with some minimal testing and may not be optimal for all
	 * hardware setups, but it should work ok for most. It's
	 * faster than 1K buffers and 1M buffers, so change at your
	 * own peril. Note that it's useful to make it fit in the L2
	 * cache, so larger isn't necessarily better.
	 */
	buf_size = st.st_size > 128 << 10 ? 128 << 10 : st.st_size;
	buf = nm_malloc(buf_size);
	/* most of the times, this loop will be gone through once */
	while (tot_written < st.st_size) {
		int loop_wr = 0;

		rd_result = read(source_fd, buf, buf_size);
		if (rd_result < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: my_fcopy() failed to read from '%s': %s\n", source, strerror(errno));
			break;
		}
		tot_read += rd_result;

		while (loop_wr < rd_result) {
			wr_result = write(dest_fd, buf + loop_wr, rd_result - loop_wr);

			if (wr_result < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: my_fcopy() failed to write to '%s': %s\n", dest, strerror(errno));
				break;
			}
			loop_wr += wr_result;
		}
		if (wr_result < 0)
			break;
		tot_written += loop_wr;
	}

	/*
	 * clean up irregardless of how things went. dest_fd comes from
	 * our caller, so we mustn't close it.
	 */
	close(source_fd);
	free(buf);

	if (rd_result < 0 || wr_result < 0) {
		/* don't leave half-written files around */
		unlink(dest);
		return ERROR;
	}

	return OK;
}


/* copies a file */
int my_fcopy(char *source, char *dest)
{
	int dest_fd, result;

	/* make sure we have something */
	if (source == NULL || dest == NULL)
		return ERROR;

	/* unlink destination file first (not doing so can cause problems on network file systems like CIFS) */
	unlink(dest);

	/* open destination file for writing */
	if ((dest_fd = open(dest, O_WRONLY | O_TRUNC | O_CREAT | O_APPEND, 0644)) < 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Unable to open file '%s' for writing: %s\n", dest, strerror(errno));
		return ERROR;
	}

	result = my_fdcopy(source, dest, dest_fd);
	close(dest_fd);
	return result;
}


/******************************************************************/
/******************** DYNAMIC BUFFER FUNCTIONS ********************/
/******************************************************************/

/* initializes a dynamic buffer */
int dbuf_init(dbuf *db, int chunk_size)
{

	if (db == NULL)
		return ERROR;

	db->buf = NULL;
	db->used_size = 0L;
	db->allocated_size = 0L;
	db->chunk_size = chunk_size;

	return OK;
}


/* frees a dynamic buffer */
int dbuf_free(dbuf *db)
{

	if (db == NULL)
		return ERROR;

	if (db->buf != NULL)
		my_free(db->buf);
	db->buf = NULL;
	db->used_size = 0L;
	db->allocated_size = 0L;

	return OK;
}


/* dynamically expands a string */
int dbuf_strcat(dbuf *db, const char *buf)
{
	char *newbuf = NULL;
	unsigned long buflen = 0L;
	unsigned long new_size = 0L;
	unsigned long memory_needed = 0L;

	if (db == NULL || buf == NULL)
		return ERROR;

	/* how much memory should we allocate (if any)? */
	buflen = strlen(buf);
	new_size = db->used_size + buflen + 1;

	/* we need more memory */
	if (db->allocated_size < new_size) {

		memory_needed = ((ceil(new_size / db->chunk_size) + 1) * db->chunk_size);

		/* allocate memory to store old and new string */
		newbuf = nm_realloc((void *)db->buf, (size_t)memory_needed);
		/* update buffer pointer */
		db->buf = newbuf;

		/* update allocated size */
		db->allocated_size = memory_needed;

		/* terminate buffer */
		db->buf[db->used_size] = '\x0';
	}

	/* append the new string */
	strcat(db->buf, buf);

	/* update size allocated */
	db->used_size += buflen;

	return OK;
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
				bucket_value = (int)(this_bucket_value + floor(last_bucket_value * last_bucket_weight));
			}
			/* otherwise use weighted % of this and last bucket */
			else {
				bucket_value = (int)(ceil(this_bucket_value * this_bucket_weight) + floor(last_bucket_value * last_bucket_weight));
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


/* returns Naemon modification date */
const char *get_program_modification_date(void)
{

	return (const char *)PROGRAM_MODIFICATION_DATE;
}



/******************************************************************/
/*********************** CLEANUP FUNCTIONS ************************/
/******************************************************************/

/* do some cleanup before we exit */
void cleanup(void)
{

#ifdef USE_EVENT_BROKER
	/* unload modules */
	if (verify_config == FALSE) {
		neb_free_callback_list();
		neb_unload_all_modules(NEBMODULE_FORCE_UNLOAD, (sigshutdown == TRUE) ? NEBMODULE_NEB_SHUTDOWN : NEBMODULE_NEB_RESTART);
		neb_free_module_list();
		neb_deinit_modules();
	}
#endif

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

	/* free all allocated memory for the object definitions */
	free_object_data();

	/* free memory allocated to comments */
	free_comment_data();

	/* free event queue data */
	squeue_destroy(nagios_squeue, SQUEUE_FREE_DATA);
	nagios_squeue = NULL;

	/* free memory for global event handlers */
	my_free(global_host_event_handler);
	my_free(global_service_event_handler);

	/* free any notification list that may have been overlooked */
	free_notification_list();

	/* free obsessive compulsive commands */
	my_free(ocsp_command);
	my_free(ochp_command);

	my_free(object_cache_file);
	my_free(object_precache_file);

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
		my_free(entry->object_ptr);
		my_free(entry);
	}
	objcfg_files = NULL;
	for (entry = objcfg_dirs; entry; entry = next) {
		next = entry->next;
		my_free(entry->object_ptr);
		my_free(entry);
	}
	objcfg_dirs = NULL;

	/* free illegal char strings */
	my_free(illegal_object_chars);
	my_free(illegal_output_chars);

	/* free naemon user and group */
	my_free(naemon_user);
	my_free(naemon_group);

	/* free file/path variables */
	my_free(status_file);
	my_free(debug_file);
	my_free(log_file);
	mac->x[MACRO_LOGFILE] = NULL; /* assigned from 'log_file' */
	my_free(temp_file);
	mac->x[MACRO_TEMPFILE] = NULL; /* assigned from temp_file */
	my_free(temp_path);
	mac->x[MACRO_TEMPPATH] = NULL; /*assigned from temp_path */
	my_free(check_result_path);
	my_free(command_file);
	my_free(qh_socket_path);
	mac->x[MACRO_COMMANDFILE] = NULL; /* assigned from command_file */
	my_free(log_archive_path);

	for (i = 0; i < MAX_USER_MACROS; i++) {
		my_free(macro_user[i]);
	}

	/* these have no other reference */
	my_free(mac->x[MACRO_ADMINEMAIL]);
	my_free(mac->x[MACRO_ADMINPAGER]);
	my_free(mac->x[MACRO_RESOURCEFILE]);
	my_free(mac->x[MACRO_OBJECTCACHEFILE]);
	my_free(mac->x[MACRO_MAINCONFIGFILE]);

	return;
}


/* free a notification list that was created */
void free_notification_list(void)
{
	notification *temp_notification = NULL;
	notification *next_notification = NULL;

	temp_notification = notification_list;
	while (temp_notification != NULL) {
		next_notification = temp_notification->next;
		my_free(temp_notification);
		temp_notification = next_notification;
	}

	/* reset notification list pointer */
	notification_list = NULL;

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

	naemon_user = nm_strdup(DEFAULT_NAEMON_USER);
	naemon_group = nm_strdup(DEFAULT_NAEMON_GROUP);

	use_regexp_matches = FALSE;
	use_true_regexp_matching = FALSE;

	use_syslog = DEFAULT_USE_SYSLOG;
	log_service_retries = DEFAULT_LOG_SERVICE_RETRIES;
	log_host_retries = DEFAULT_LOG_HOST_RETRIES;
	log_initial_states = DEFAULT_LOG_INITIAL_STATES;

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
	next_notification_id = 1;

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

	use_large_installation_tweaks = DEFAULT_USE_LARGE_INSTALLATION_TWEAKS;
	enable_environment_macros = FALSE;
	free_child_process_memory = FALSE;
	child_processes_fork_twice = FALSE;

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
