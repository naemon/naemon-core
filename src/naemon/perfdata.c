#include "config.h"
#include "common.h"
#include "perfdata.h"
#include "macros.h"
#include "objects_command.h"
#include "events.h"
#include "logging.h"
#include "workers.h"
#include "nm_alloc.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

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

static command *host_perfdata_command_ptr = NULL;
static command *service_perfdata_command_ptr = NULL;
static command *host_perfdata_file_processing_command_ptr = NULL;
static command *service_perfdata_file_processing_command_ptr = NULL;
static int host_perfdata_fd = -1;
static int service_perfdata_fd = -1;
static nm_bufferqueue *host_perfdata_bq = NULL;
static nm_bufferqueue *service_perfdata_bq = NULL;

static void xpddefault_process_host_perfdata_file(struct nm_event_execution_properties *evprop);
static void xpddefault_process_service_perfdata_file(struct nm_event_execution_properties *evprop);
static int xpddefault_run_service_performance_data_command(nagios_macros *mac, service *);
static int xpddefault_run_host_performance_data_command(nagios_macros *mac, host *);

static int xpddefault_update_service_performance_data_file(nagios_macros *mac, service *);
static int xpddefault_update_host_performance_data_file(nagios_macros *mac, host *);

static int xpddefault_preprocess_file_templates(char *);

static int xpddefault_open_perfdata_file(char *perfdata_file, int is_pipe, int append);


/******************************************************************/
/************** INITIALIZATION & CLEANUP FUNCTIONS ****************/
/******************************************************************/

/* initializes performance data */
int initialize_performance_data(const char *cfgfile)
{
	char *buffer = NULL;
	char *temp_buffer = NULL;
	command *temp_command = NULL;
	time_t current_time;
	nagios_macros *mac;

	mac = get_global_macros();
	time(&current_time);

	/* reset vars */
	host_perfdata_command_ptr = NULL;
	service_perfdata_command_ptr = NULL;
	host_perfdata_file_processing_command_ptr = NULL;
	service_perfdata_file_processing_command_ptr = NULL;

	/* make sure we have some templates defined */
	if (host_perfdata_file_template == NULL)
		host_perfdata_file_template = nm_strdup(DEFAULT_HOST_PERFDATA_FILE_TEMPLATE);
	if (service_perfdata_file_template == NULL)
		service_perfdata_file_template = nm_strdup(DEFAULT_SERVICE_PERFDATA_FILE_TEMPLATE);

	/* process special chars in templates */
	xpddefault_preprocess_file_templates(host_perfdata_file_template);
	xpddefault_preprocess_file_templates(service_perfdata_file_template);

	/* open the performance data caches */
	host_perfdata_bq = nm_bufferqueue_create();
	host_perfdata_fd = xpddefault_open_perfdata_file(
		host_perfdata_file,
		host_perfdata_file_pipe,
		host_perfdata_file_append);
	service_perfdata_bq = nm_bufferqueue_create();
	service_perfdata_fd = xpddefault_open_perfdata_file(
		service_perfdata_file,
		service_perfdata_file_pipe,
		service_perfdata_file_append);

	/* verify that performance data commands are valid */
	if (host_perfdata_command != NULL) {
		temp_buffer = nm_strdup(host_perfdata_command);
		if ((temp_command = find_bang_command(temp_buffer)) == NULL) {
			nm_log(NSLOG_RUNTIME_WARNING, "Warning: Host performance command '%s' was not found - host performance data will not be processed!\n", host_perfdata_command);
			nm_free(host_perfdata_command);
		}

		nm_free(temp_buffer);

		/* save the command pointer for later */
		host_perfdata_command_ptr = temp_command;
	}

	if (service_perfdata_command != NULL) {
		temp_buffer = nm_strdup(service_perfdata_command);
		if ((temp_command = find_bang_command(temp_buffer)) == NULL) {
			nm_log(NSLOG_RUNTIME_WARNING, "Warning: Service performance command '%s' was not found - service performance data will not be processed!\n", service_perfdata_command);
			nm_free(service_perfdata_command);
		}

		nm_free(temp_buffer);

		/* save the command pointer for later */
		service_perfdata_command_ptr = temp_command;
	}

	if (host_perfdata_file_processing_command != NULL) {
		temp_buffer = nm_strdup(host_perfdata_file_processing_command);
		if ((temp_command = find_bang_command(temp_buffer)) == NULL) {
			nm_log(NSLOG_RUNTIME_WARNING, "Warning: Host performance file processing command '%s' was not found - host performance data file will not be processed!\n", host_perfdata_file_processing_command);
			nm_free(host_perfdata_file_processing_command);
		}

		nm_free(temp_buffer);

		/* save the command pointer for later */
		host_perfdata_file_processing_command_ptr = temp_command;
	}

	if (service_perfdata_file_processing_command != NULL) {
		temp_buffer = nm_strdup(service_perfdata_file_processing_command);
		if ((temp_command = find_bang_command(temp_buffer)) == NULL) {
			nm_log(NSLOG_RUNTIME_WARNING, "Warning: Service performance file processing command '%s' was not found - service performance data file will not be processed!\n", service_perfdata_file_processing_command);
			nm_free(service_perfdata_file_processing_command);
		}

		/* save the command pointer for later */
		service_perfdata_file_processing_command_ptr = temp_command;
	}

	/* periodically process the host perfdata file */
	if (host_perfdata_file_processing_interval > 0 && host_perfdata_file_processing_command != NULL) {
		if (host_perfdata_file_pipe)
			nm_log(NSLOG_RUNTIME_WARNING, "Warning: Host performance file is configured to be a pipe - ignoring host_perfdata_file_processing_interval");
		else
			schedule_event(host_perfdata_file_processing_interval, xpddefault_process_host_perfdata_file, NULL);
	}

	/* periodically process the service perfdata file */
	if (service_perfdata_file_processing_interval > 0 && service_perfdata_file_processing_command != NULL) {
		if (service_perfdata_file_pipe)
			nm_log(NSLOG_RUNTIME_WARNING, "Warning: Service performance file is configured to be a pipe - ignoring service_perfdata_file_processing_interval");
		else
			schedule_event(service_perfdata_file_processing_interval, xpddefault_process_service_perfdata_file, NULL);
	}

	/* save the host perf data file macro */
	nm_free(mac->x[MACRO_HOSTPERFDATAFILE]);
	if (host_perfdata_file != NULL) {
		mac->x[MACRO_HOSTPERFDATAFILE] = nm_strdup(host_perfdata_file);
		strip(mac->x[MACRO_HOSTPERFDATAFILE]);
	}

	/* save the service perf data file macro */
	nm_free(mac->x[MACRO_SERVICEPERFDATAFILE]);
	if (service_perfdata_file != NULL) {
		mac->x[MACRO_SERVICEPERFDATAFILE] = nm_strdup(service_perfdata_file);
		strip(mac->x[MACRO_SERVICEPERFDATAFILE]);
	}

	nm_free(temp_buffer);
	nm_free(buffer);

	return OK;
}


/* cleans up performance data */
int cleanup_performance_data(void)
{
	nm_free(host_perfdata_command);
	nm_free(service_perfdata_command);
	nm_free(host_perfdata_file_template);
	nm_free(service_perfdata_file_template);
	nm_free(host_perfdata_file);
	nm_free(service_perfdata_file);
	nm_free(host_perfdata_file_processing_command);
	nm_free(service_perfdata_file_processing_command);
	// one last attempt to write what remains buffered, just in case:
	nm_bufferqueue_write(host_perfdata_bq, host_perfdata_fd);
	nm_bufferqueue_write(service_perfdata_bq, service_perfdata_fd);
	close(host_perfdata_fd);
	host_perfdata_fd = -1;
	close(service_perfdata_fd);
	service_perfdata_fd = -1;
	nm_bufferqueue_destroy(host_perfdata_bq);
	host_perfdata_bq = NULL;
	nm_bufferqueue_destroy(service_perfdata_bq);
	service_perfdata_bq = NULL;

	return OK;
}


/******************************************************************/
/****************** PERFORMANCE DATA FUNCTIONS ********************/
/******************************************************************/

/* updates service performance data */
int update_service_performance_data(service *svc)
{
	nagios_macros mac;
	host *hst;

	/* should we be processing performance data for anything? */
	if (process_performance_data == FALSE)
		return OK;

	/* should we process performance data for this service? */
	if (svc->process_performance_data == FALSE)
		return OK;

	/*
	 * bail early if we've got nothing to do so we don't spend a lot
	 * of time calculating macros that never get used
	 * on distributed setups, empty perfdata results are required, so
	 * only drop out if demanded via configs.
	*/
	if (service_perfdata_process_empty_results == FALSE) {
		if (!svc || !svc->perf_data || !*svc->perf_data) {
			return OK;
		}
		if ((!service_perfdata_file_template) && !service_perfdata_command) {
			return OK;
		}

	}
	/*
	 * we know we've got some work to do, so grab the necessary
	 * macros and get busy
	 */
	memset(&mac, 0, sizeof(mac));
	hst = find_host(svc->host_name);
	grab_host_macros_r(&mac, hst);
	grab_service_macros_r(&mac, svc);

	/* run the performance data command */
	xpddefault_run_service_performance_data_command(&mac, svc);

	/* get rid of used memory we won't need anymore */
	clear_argv_macros_r(&mac);

	/* update the performance data file */
	xpddefault_update_service_performance_data_file(&mac, svc);

	/* now free() it all */
	clear_volatile_macros_r(&mac);

	return OK;
}


/* updates host performance data */
int update_host_performance_data(host *hst)
{
	nagios_macros mac;

	/* should we be processing performance data for anything? */
	if (process_performance_data == FALSE)
		return OK;

	/* should we process performance data for this host? */
	if (hst->process_performance_data == FALSE)
		return OK;

	/*
	 * bail early if we've got nothing to do so we don't spend a lot
	 * of time calculating macros that never get used
	 * on distributed setups, empty perfdata results are required, so
	 * only drop out if demanded via configs.
	 */
	if (host_perfdata_process_empty_results == FALSE) {
		if (!hst || !hst->perf_data || !*hst->perf_data) {
			return OK;
		}
		if ((!host_perfdata_file_template) && !host_perfdata_command) {
			return OK;
		}
	}

	/* set up macros and get to work */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, hst);

	/* run the performance data command */
	xpddefault_run_host_performance_data_command(&mac, hst);

	/* no more commands to run, so we won't need this any more */
	clear_argv_macros_r(&mac);

	/* update the performance data file */
	xpddefault_update_host_performance_data_file(&mac, hst);

	/* free() all */
	clear_volatile_macros_r(&mac);

	return OK;
}

/******************************************************************/
/************** PERFORMANCE DATA COMMAND FUNCTIONS ****************/
/******************************************************************/

static void xpddefault_perfdata_job_handler(struct wproc_result *wpres, void *data, int flags) {
	/* Don't do anything */
}

/* runs the service performance data command */
static int xpddefault_run_service_performance_data_command(nagios_macros *mac, service *svc)
{
	char *raw_command_line = NULL;
	char *processed_command_line = NULL;
	int result = OK;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;

	if (svc == NULL)
		return ERROR;

	/* we don't have a command */
	if (service_perfdata_command == NULL)
		return OK;

	/* get the raw command line */
	get_raw_command_line_r(mac, service_perfdata_command_ptr, service_perfdata_command, &raw_command_line, macro_options);
	if (raw_command_line == NULL)
		return ERROR;

	log_debug_info(DEBUGL_PERFDATA, 2, "Raw service performance data command line: %s\n", raw_command_line);

	/* process any macros in the raw command line */
	process_macros_r(mac, raw_command_line, &processed_command_line, macro_options);
	nm_free(raw_command_line);
	if (processed_command_line == NULL)
		return ERROR;

	log_debug_info(DEBUGL_PERFDATA, 2, "Processed service performance data command line: %s\n", processed_command_line);
	wproc_run_callback(processed_command_line, perfdata_timeout, xpddefault_perfdata_job_handler, NULL, mac);

	nm_free(processed_command_line);

	return result;
}


/* runs the host performance data command */
static int xpddefault_run_host_performance_data_command(nagios_macros *mac, host *hst)
{
	char *raw_command_line = NULL;
	char *processed_command_line = NULL;
	int result = OK;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;

	if (hst == NULL)
		return ERROR;

	/* we don't have a command */
	if (host_perfdata_command == NULL)
		return OK;

	/* get the raw command line */
	get_raw_command_line_r(mac, host_perfdata_command_ptr, host_perfdata_command, &raw_command_line, macro_options);
	if (raw_command_line == NULL)
		return ERROR;

	log_debug_info(DEBUGL_PERFDATA, 2, "Raw host performance data command line: %s\n", raw_command_line);

	/* process any macros in the raw command line */
	process_macros_r(mac, raw_command_line, &processed_command_line, macro_options);
	nm_free(raw_command_line);
	if (!processed_command_line)
		return ERROR;

	log_debug_info(DEBUGL_PERFDATA, 2, "Processed host performance data command line: %s\n", processed_command_line);

	/* run the command */
	wproc_run_callback(processed_command_line, perfdata_timeout, xpddefault_perfdata_job_handler, NULL, mac);

	nm_free(processed_command_line);

	return result;
}


/******************************************************************/
/**************** FILE PERFORMANCE DATA FUNCTIONS *****************/
/******************************************************************/

/* open the host performance data file for writing */
static int xpddefault_open_perfdata_file(char *perfdata_file, int is_pipe, int append)
{
	int perfdata_fd;
	if (!perfdata_file)
		return -1;

	if (is_pipe) {
		/* must open read-write to avoid failure if the other end isn't ready yet */
		perfdata_fd = open(perfdata_file, O_NONBLOCK | O_RDWR | O_CREAT, 0644);
	} else
		perfdata_fd = open(perfdata_file, O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC), 0644);

	if (perfdata_fd == -1) {

		nm_log(NSLOG_RUNTIME_WARNING, "Warning: File '%s' could not be opened (%s) - performance data will not be written to file!\n", strerror(errno), perfdata_file);

		return -1;
	}
	return perfdata_fd;
}

/* processes delimiter characters in templates */
static int xpddefault_preprocess_file_templates(char *template)
{
	char *tempbuf;
	unsigned int x, y;

	if (template == NULL)
		return OK;

	/* allocate temporary buffer */
	tempbuf = nm_malloc(strlen(template) + 1);
	strcpy(tempbuf, "");

	for (x = 0, y = 0; x < strlen(template); x++, y++) {
		if (template[x] == '\\') {
			if (template[x + 1] == 't') {
				tempbuf[y] = '\t';
				x++;
			} else if (template[x + 1] == 'r') {
				tempbuf[y] = '\r';
				x++;
			} else if (template[x + 1] == 'n') {
				tempbuf[y] = '\n';
				x++;
			} else
				tempbuf[y] = template[x];
		} else
			tempbuf[y] = template[x];
	}
	tempbuf[y] = '\x0';

	strcpy(template, tempbuf);
	nm_free(tempbuf);

	return OK;
}


/* updates service performance data file */
static int xpddefault_update_service_performance_data_file(nagios_macros *mac, service *svc)
{
	char *raw_output = NULL;
	char *processed_output = NULL;
	int result = OK;

	if (svc == NULL)
		return ERROR;

	if (service_perfdata_file_template == NULL)
		return OK;

	nm_asprintf(&raw_output, "%s\n", service_perfdata_file_template);
	log_debug_info(DEBUGL_PERFDATA, 2, "Raw service performance data file output: %s", raw_output);

	/* process any macros in the raw output */
	process_macros_r(mac, raw_output, &processed_output, 0);
	if (processed_output == NULL)
		return ERROR;

	log_debug_info(DEBUGL_PERFDATA, 2, "Processed service performance data file output: %s", processed_output);

	nm_bufferqueue_push(service_perfdata_bq, processed_output, strlen(processed_output));
	/* temporary failures are fine - if it's serious, we log before we run the processing event */
	if (service_perfdata_fd >= 0)
		nm_bufferqueue_write(service_perfdata_bq, service_perfdata_fd);

	nm_free(raw_output);
	nm_free(processed_output);

	return result;
}


/* updates host performance data file */
static int xpddefault_update_host_performance_data_file(nagios_macros *mac, host *hst)
{
	char *raw_output = NULL;
	char *processed_output = NULL;
	int result = OK;

	if (hst == NULL)
		return ERROR;

	if (host_perfdata_file_template == NULL)
		return OK;

	nm_asprintf(&raw_output, "%s\n", host_perfdata_file_template);
	log_debug_info(DEBUGL_PERFDATA, 2, "Raw host performance file output: %s", raw_output);

	/* process any macros in the raw output */
	process_macros_r(mac, raw_output, &processed_output, 0);
	if (processed_output == NULL)
		return ERROR;

	log_debug_info(DEBUGL_PERFDATA, 2, "Processed host performance data file output: %s", processed_output);

	nm_bufferqueue_push(host_perfdata_bq, processed_output, strlen(processed_output));
	/* temporary failures are fine - if it's serious, we log before we run the processing event */
	if (host_perfdata_fd >= 0)
		nm_bufferqueue_write(host_perfdata_bq, host_perfdata_fd);

	nm_free(raw_output);
	nm_free(processed_output);

	return result;
}

static void xpddefault_process_host_job_handler(struct wproc_result *wpres, void *_tmpname, int flags)
{
	if (wpres && wpres->early_timeout) {
		nm_log(NSLOG_RUNTIME_WARNING,
			   "Warning: Host performance data file processing command '%s' timed out after %d seconds\n", wpres->command, perfdata_timeout);
	}
	host_perfdata_fd = xpddefault_open_perfdata_file(
		host_perfdata_file,
		host_perfdata_file_pipe,
		host_perfdata_file_append);
}

/* periodically process the host perf data file */
static void xpddefault_process_host_perfdata_file(struct nm_event_execution_properties *evprop)
{
	char *raw_command_line = NULL;
	char *processed_command_line = NULL;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	nagios_macros mac;

	if(evprop->execution_type == EVENT_EXEC_NORMAL) {
		/* Recurring event */
		schedule_event(host_perfdata_file_processing_interval, xpddefault_process_host_perfdata_file, NULL);

		/* we don't have a command */
		if (host_perfdata_file_processing_command == NULL)
			return; /* OK */

		/* init macros */
		memset(&mac, 0, sizeof(mac));

		/* get the raw command line */
		get_raw_command_line_r(&mac, host_perfdata_file_processing_command_ptr, host_perfdata_file_processing_command, &raw_command_line, macro_options);
		if (raw_command_line == NULL) {
			clear_volatile_macros_r(&mac);
			return; /* ERROR */
		}

		log_debug_info(DEBUGL_PERFDATA, 2, "Raw host performance data file processing command line: %s\n", raw_command_line);

		/* process any macros in the raw command line */
		process_macros_r(&mac, raw_command_line, &processed_command_line, macro_options);
		nm_free(raw_command_line);
		if (processed_command_line == NULL) {
			clear_volatile_macros_r(&mac);
			return; /* ERROR */
		}

		log_debug_info(DEBUGL_PERFDATA, 2, "Processed host performance data file processing command line: %s\n", processed_command_line);

		if (host_perfdata_fd >= 0) {
			if (nm_bufferqueue_write(host_perfdata_bq, host_perfdata_fd) < 0) {
				nm_log(
					NSLOG_RUNTIME_WARNING,
					"Warning: Failed to flush performance data to service performance file %s",
					host_perfdata_file);
			} else {
				close(host_perfdata_fd);
				wproc_run_callback(processed_command_line, perfdata_timeout, xpddefault_process_host_job_handler, NULL, &mac);
			}
		}

		clear_volatile_macros_r(&mac);
		nm_free(processed_command_line);
	}
}


static void xpddefault_process_service_job_handler(struct wproc_result *wpres, void *_tmpname, int flags)
{
	if (wpres && wpres->early_timeout) {
		nm_log(NSLOG_RUNTIME_WARNING,
			   "Warning: Service performance data file processing command '%s' timed out after %d seconds\n", wpres->command, perfdata_timeout);
	}
	service_perfdata_fd = xpddefault_open_perfdata_file(
		service_perfdata_file,
		service_perfdata_file_pipe,
		service_perfdata_file_append);
}

/* periodically process the service perf data file */
static void xpddefault_process_service_perfdata_file(struct nm_event_execution_properties *evprop)
{
	char *raw_command_line = NULL;
	char *processed_command_line = NULL;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	nagios_macros mac;

	if(evprop->execution_type == EVENT_EXEC_NORMAL) {
		/* Recurring event */
		schedule_event(service_perfdata_file_processing_interval, xpddefault_process_service_perfdata_file, NULL);

		/* we don't have a command */
		if (service_perfdata_file_processing_command == NULL)
			return; /* OK */

		/* init macros */
		memset(&mac, 0, sizeof(mac));

		/* get the raw command line */
		get_raw_command_line_r(&mac, service_perfdata_file_processing_command_ptr, service_perfdata_file_processing_command, &raw_command_line, macro_options);
		if (raw_command_line == NULL) {
			clear_volatile_macros_r(&mac);
			return; /* ERROR */
		}

		log_debug_info(DEBUGL_PERFDATA, 2, "Raw service performance data file processing command line: %s\n", raw_command_line);

		/* process any macros in the raw command line */
		process_macros_r(&mac, raw_command_line, &processed_command_line, macro_options);
		nm_free(raw_command_line);
		if (processed_command_line == NULL) {
			clear_volatile_macros_r(&mac);
			return; /* ERROR */
		}

		log_debug_info(DEBUGL_PERFDATA, 2, "Processed service performance data file processing command line: %s\n", processed_command_line);

		if (service_perfdata_fd >= 0) {
			if (nm_bufferqueue_write(service_perfdata_bq, service_perfdata_fd) < 0) {
				nm_log(
					NSLOG_RUNTIME_WARNING,
					"Warning: Failed to flush performance data to service performance file %s",
					service_perfdata_file);
			} else {
				close(service_perfdata_fd);
				wproc_run_callback(processed_command_line, perfdata_timeout, xpddefault_process_service_job_handler, NULL, &mac);
			}
		}
		clear_volatile_macros_r(&mac);
		nm_free(processed_command_line);
	}
}
