#include "logging.h"
#include "config.h"
#include "common.h"
#include "statusdata.h"
#include "macros.h"
#include "broker.h"
#include "utils.h"
#include "globals.h"
#include "nm_alloc.h"
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>

static FILE *debug_file_fp;
static FILE *log_fp;

/******************************************************************/
/************************ LOGGING FUNCTIONS ***********************/
/******************************************************************/

/* write something to the console */
static void write_to_console(char *buffer)
{
	/* should we print to the console? */
	if (daemon_mode == FALSE)
		printf("%s\n", buffer);
}


/* write something to the log file, syslog, and possibly the console */
static void write_to_logs_and_console(char *buffer, unsigned long data_type, int display)
{
	register int len = 0;
	register int x = 0;

	/* strip unnecessary newlines */
	len = strlen(buffer);
	for (x = len - 1; x >= 0; x--) {
		if (buffer[x] == '\n')
			buffer[x] = '\x0';
		else
			break;
	}

	/* write messages to the logs */
	write_to_all_logs(buffer, data_type);

	/* write message to the console */
	if (display == TRUE) {
		write_to_console(buffer);
	}
}


/* The main logging function */
void logit(int data_type, int display, const char *fmt, ...)
{
	va_list ap;
	char *buffer = NULL;

	va_start(ap, fmt);
	if (vasprintf(&buffer, fmt, ap) > 0) {
		write_to_logs_and_console(buffer, data_type, display);
		free(buffer);
	}
	va_end(ap);
}


/* write something to the log file and syslog facility */
int write_to_all_logs(char *buffer, unsigned long data_type)
{

	/* write to syslog */
	write_to_syslog(buffer, data_type);

	/* write to main log */
	write_to_log(buffer, data_type, NULL);

	return OK;
}


/* write something to the log file and syslog facility */
static void write_to_all_logs_with_timestamp(char *buffer, unsigned long data_type, time_t *timestamp)
{
	/* write to syslog */
	write_to_syslog(buffer, data_type);

	/* write to main log */
	write_to_log(buffer, data_type, timestamp);
}


static FILE *open_log_file(void)
{
	if (log_fp) /* keep it open unless we rotate */
		return log_fp;

	log_fp = fopen(log_file, "a+");
	if (log_fp == NULL) {
		if (daemon_mode == FALSE) {
			printf("Warning: Cannot open log file '%s' for writing\n", log_file);
		}
		return NULL;
	}

	(void)fcntl(fileno(log_fp), F_SETFD, FD_CLOEXEC);
	return log_fp;
}

int fix_log_file_owner(uid_t uid, gid_t gid)
{
	int r1 = 0, r2 = 0;

	if (!(log_fp = open_log_file()))
		return -1;
	r1 = fchown(fileno(log_fp), uid, gid);

	if (open_debug_log() != OK)
		return -1;
	if (debug_file_fp)
		r2 = fchown(fileno(debug_file_fp), uid, gid);

	/* return 0 if both are 0 and otherwise < 0 */
	return r1 < r2 ? r1 : r2;
}

int close_log_file(void)
{
	if (!log_fp)
		return 0;

	fflush(log_fp);
	fclose(log_fp);
	log_fp = NULL;
	return 0;
}

/* write something to the naemon log file */
int write_to_log(char *buffer, unsigned long data_type, time_t *timestamp)
{
	FILE *fp;
	time_t log_time = 0L;

	if (buffer == NULL)
		return ERROR;

	/* don't log anything if we're not actually running... */
	if (verify_config)
		return OK;

	/* make sure we can log this type of entry */
	if (!(data_type & logging_options))
		return OK;

	fp = open_log_file();
	if (fp == NULL)
		return ERROR;
	/* what timestamp should we use? */
	if (timestamp == NULL)
		time(&log_time);
	else
		log_time = *timestamp;

	/* strip any newlines from the end of the buffer */
	strip(buffer);

	/* write the buffer to the log file */
	fprintf(fp, "[%lu] %s\n", log_time, buffer);
	fflush(fp);

#ifdef USE_EVENT_BROKER
	/* send data to the event broker */
	broker_log_data(NEBTYPE_LOG_DATA, NEBFLAG_NONE, NEBATTR_NONE, buffer, data_type, log_time, NULL);
#endif

	return OK;
}


/* write something to the syslog facility */
int write_to_syslog(char *buffer, unsigned long data_type)
{

	if (buffer == NULL)
		return ERROR;

	/* don't log anything if we're not actually running... */
	if (verify_config)
		return OK;

	/* bail out if we shouldn't write to syslog */
	if (use_syslog == FALSE)
		return OK;

	/* make sure we should log this type of entry */
	if (!(data_type & syslog_options))
		return OK;

	/* write the buffer to the syslog facility */
	syslog(LOG_USER | LOG_INFO, "%s", buffer);

	return OK;
}


/* write a service problem/recovery to the naemon log file */
int log_service_event(service *svc)
{
	char *temp_buffer = NULL;
	unsigned long log_options = 0L;

	/* don't log soft errors if the user doesn't want to */
	if (svc->state_type == SOFT_STATE && !log_service_retries)
		return OK;

	/* get the log options */
	if (svc->current_state == STATE_UNKNOWN)
		log_options = NSLOG_SERVICE_UNKNOWN;
	else if (svc->current_state == STATE_WARNING)
		log_options = NSLOG_SERVICE_WARNING;
	else if (svc->current_state == STATE_CRITICAL)
		log_options = NSLOG_SERVICE_CRITICAL;
	else
		log_options = NSLOG_SERVICE_OK;

	nm_asprintf(&temp_buffer, "SERVICE ALERT: %s;%s;%s;%s;%d;%s\n",
	         svc->host_name, svc->description,
	         service_state_name(svc->current_state),
	         state_type_name(svc->state_type),
	         svc->current_attempt,
	         (svc->plugin_output == NULL) ? "" : svc->plugin_output);

	write_to_all_logs(temp_buffer, log_options);
	free(temp_buffer);

	return OK;
}


/* write a host problem/recovery to the log file */
int log_host_event(host *hst)
{
	char *temp_buffer = NULL;
	unsigned long log_options = 0L;

	/* get the log options */
	if (hst->current_state == HOST_DOWN)
		log_options = NSLOG_HOST_DOWN;
	else if (hst->current_state == HOST_UNREACHABLE)
		log_options = NSLOG_HOST_UNREACHABLE;
	else
		log_options = NSLOG_HOST_UP;

	nm_asprintf(&temp_buffer, "HOST ALERT: %s;%s;%s;%d;%s\n",
	         hst->name,
	         host_state_name(hst->current_state),
	         state_type_name(hst->state_type),
	         hst->current_attempt,
	         (hst->plugin_output == NULL) ? "" : hst->plugin_output);

	write_to_all_logs(temp_buffer, log_options);

	my_free(temp_buffer);

	return OK;
}


/* logs host states */
int log_host_states(int type, time_t *timestamp)
{
	char *temp_buffer = NULL;
	host *temp_host = NULL;;

	/* bail if we shouldn't be logging initial states */
	if (type == INITIAL_STATES && log_initial_states == FALSE)
		return OK;

	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

		nm_asprintf(&temp_buffer, "%s HOST STATE: %s;%s;%s;%d;%s\n", (type == INITIAL_STATES) ? "INITIAL" : "CURRENT",
		         temp_host->name,
		         host_state_name(temp_host->current_state),
		         state_type_name(temp_host->state_type),
		         temp_host->current_attempt,
		         (temp_host->plugin_output == NULL) ? "" : temp_host->plugin_output);

		write_to_all_logs_with_timestamp(temp_buffer, NSLOG_INFO_MESSAGE, timestamp);

		my_free(temp_buffer);
	}

	return OK;
}


/* logs service states */
int log_service_states(int type, time_t *timestamp)
{
	char *temp_buffer = NULL;
	service *temp_service = NULL;

	/* bail if we shouldn't be logging initial states */
	if (type == INITIAL_STATES && log_initial_states == FALSE)
		return OK;

	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		nm_asprintf(&temp_buffer, "%s SERVICE STATE: %s;%s;%s;%s;%d;%s\n",
		         (type == INITIAL_STATES) ? "INITIAL" : "CURRENT",
		         temp_service->host_name, temp_service->description,
		         service_state_name(temp_service->current_state),
		         state_type_name(temp_service->state_type),
		         temp_service->current_attempt,
		         temp_service->plugin_output);

		write_to_all_logs_with_timestamp(temp_buffer, NSLOG_INFO_MESSAGE, timestamp);

		my_free(temp_buffer);
	}

	return OK;
}


/* rotates the main log file */
int rotate_log_file(time_t rotation_time)
{
	char *temp_buffer = NULL;

	/* update the last log rotation time and status log */
	last_log_rotation = time(NULL);

	close_log_file();
	log_fp = open_log_file();
	if (log_fp == NULL)
		return ERROR;

	/* record the log rotation after it has been done... */
	temp_buffer = "LOG ROTATION: EXTERNAL";
	write_to_all_logs_with_timestamp(temp_buffer, NSLOG_PROCESS_INFO, &rotation_time);

	/* record log file version format */
	write_log_file_info(&rotation_time);

	/* log current host and service state if activated */
	if (log_current_states == TRUE) {
		log_host_states(CURRENT_STATES, &rotation_time);
		log_service_states(CURRENT_STATES, &rotation_time);
	}

	return OK;
}


/* record log file version/info */
int write_log_file_info(time_t *timestamp)
{
	char *temp_buffer = NULL;

	/* write log version */
	nm_asprintf(&temp_buffer, "LOG VERSION: %s\n", LOG_VERSION_2);
	write_to_all_logs_with_timestamp(temp_buffer, NSLOG_PROCESS_INFO, timestamp);
	my_free(temp_buffer);

	return OK;
}


/* opens the debug log for writing */
int open_debug_log(void)
{

	/* don't do anything if we're not actually running... */
	if (verify_config)
		return OK;

	/* don't do anything if we're not debugging */
	if (debug_level == DEBUGL_NONE)
		return OK;

	if ((debug_file_fp = fopen(debug_file, "a+")) == NULL)
		return ERROR;

	(void)fcntl(fileno(debug_file_fp), F_SETFD, FD_CLOEXEC);

	return OK;
}


/* closes the debug log */
int close_debug_log(void)
{

	if (debug_file_fp != NULL)
		fclose(debug_file_fp);

	debug_file_fp = NULL;

	return OK;
}


/* write to the debug log */
int log_debug_info(int level, int verbosity, const char *fmt, ...)
{
	va_list ap;
	char *tmppath = NULL;
	struct timeval current_time;

	if (!(debug_level == DEBUGL_ALL || (level & debug_level)))
		return OK;

	if (verbosity > debug_verbosity)
		return OK;

	if (debug_file_fp == NULL)
		return ERROR;

	/* write the timestamp */
	gettimeofday(&current_time, NULL);
	fprintf(debug_file_fp, "[%ld.%06ld] [%03d.%d] [pid=%lu] ", (long)current_time.tv_sec, (long)current_time.tv_usec, level, verbosity, (unsigned long)getpid());

	/* write the data */
	va_start(ap, fmt);
	vfprintf(debug_file_fp, fmt, ap);
	va_end(ap);

	/* flush, so we don't have problems tailing or when fork()ing */
	fflush(debug_file_fp);

	/* if file has grown beyond max, rotate it */
	if ((unsigned long)ftell(debug_file_fp) > max_debug_file_size && max_debug_file_size > 0L) {

		/* close the file */
		close_debug_log();

		/* rotate the log file */
		nm_asprintf(&tmppath, "%s.old", debug_file);
		if (tmppath) {

			/* unlink the old debug file */
			unlink(tmppath);

			/* rotate the debug file */
			my_rename(debug_file, tmppath);

			/* free memory */
			my_free(tmppath);
		}

		/* open a new file */
		open_debug_log();
	}

	return OK;
}
