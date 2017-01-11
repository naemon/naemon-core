#include "logging.h"
#include "broker.h"
#include "utils.h"
#include "globals.h"
#include "nm_alloc.h"
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/time.h>

static FILE *debug_file_fp;
static FILE *log_fp;

int log_initial_states = DEFAULT_LOG_INITIAL_STATES;
int log_current_states = DEFAULT_LOG_CURRENT_STATES;

/******************************************************************/
/************************ LOGGING FUNCTIONS ***********************/
/******************************************************************/

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


/* write something to the console */
static void write_to_console(char *buffer)
{
	/* should we print to the console? */
	if (daemon_mode == FALSE)
		printf("%s\n", buffer);
}

/* write something to the syslog facility */
static int write_to_syslog(char *buffer, unsigned long data_type)
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

/* write something to the naemon log file */
static int write_to_log(char *buffer, unsigned long data_type, time_t *timestamp)
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

	broker_log_data(NEBTYPE_LOG_DATA, NEBFLAG_NONE, NEBATTR_NONE, buffer, data_type, log_time);

	return OK;
}




/* write something to the log file and syslog facility */
static int write_to_all_logs(char *buffer, unsigned long data_type)
{

	/* write to syslog */
	write_to_syslog(buffer, data_type);

	/* write to main log */
	write_to_log(buffer, data_type, NULL);

	return OK;
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
void nm_log(int data_type, const char *fmt, ...)
{
	va_list ap;
	char *buffer = NULL;

	va_start(ap, fmt);
	if (vasprintf(&buffer, fmt, ap) > 0) {
		write_to_logs_and_console(buffer, data_type, TRUE);
		free(buffer);
	}
	va_end(ap);
}

/* write something to the log file and syslog facility */
static void write_to_all_logs_with_timestamp(char *buffer, unsigned long data_type, time_t *timestamp)
{
	/* write to syslog */
	write_to_syslog(buffer, data_type);

	/* write to main log */
	write_to_log(buffer, data_type, timestamp);
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
	nm_free(temp_buffer);

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

			nm_free(tmppath);
		}

		/* open a new file */
		open_debug_log();
	}

	return OK;
}

void nm_g_log_handler(const gchar *domain, GLogLevelFlags log_level,
		const gchar *message, gpointer udata) {
	int nm_log_type = 0;
	if (log_level & G_LOG_LEVEL_ERROR || log_level & G_LOG_LEVEL_CRITICAL)
		nm_log_type |= NSLOG_RUNTIME_ERROR;

	if (log_level & G_LOG_LEVEL_WARNING)
		nm_log_type |= NSLOG_RUNTIME_WARNING;

	if (log_level & G_LOG_LEVEL_MESSAGE || log_level & G_LOG_LEVEL_INFO)
		nm_log_type |= NSLOG_INFO_MESSAGE;

	if (nm_log_type != 0)
		nm_log(nm_log_type, message, NULL);

	if (log_level & G_LOG_LEVEL_DEBUG)
		log_debug_info(DEBUGL_ALL, 1, message, NULL);
}
