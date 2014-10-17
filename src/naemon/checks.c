#include "checks.h"
#include "checks_service.h"
#include "checks_host.h"
#include "config.h"
#include "comments.h"
#include "common.h"
#include "statusdata.h"
#include "downtime.h"
#include "macros.h"
#include "broker.h"
#include "perfdata.h"
#include "workers.h"
#include "utils.h"
#include "events.h"
#include "flapping.h"
#include "sehandlers.h"
#include "notifications.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"
#include "defaults.h"
#include <string.h>

/* for process_check_result_* */
#include <sys/types.h>
#include <dirent.h>

#ifdef USE_EVENT_BROKER
#include "neberrors.h"
#endif

/* forward declarations */
static const char *spool_file_source_name(void *source);
static void reap_check_results(struct timed_event_properties *evprop);


static struct check_engine nagios_spool_check_engine = {
	"Spooled checkresult file",
	spool_file_source_name,
	NULL,
};

static const char *spool_file_source_name(void *source)
{
	return "check result spool dir";
}


/******************************************************************/
/************************* INIT FUNCTIONS *************************/
/******************************************************************/

void checks_init(void)
{
	checks_init_hosts();
	checks_init_services();

	/******** SCHEDULE MISC EVENTS ********/

	/* add a check result reaper event */
	schedule_event(check_reaper_interval, reap_check_results, NULL);
}

/******************************************************************/
/********************** CHECK REAPER FUNCTIONS ********************/
/******************************************************************/

/* reaps host and service check results */
static void reap_check_results(struct timed_event_properties *evprop)
{
	int reaped_checks = 0;

	if(evprop->flags & EVENT_EXEC_FLAG_TIMED) {
		/* Reschedule, since reccuring */
		schedule_event(check_reaper_interval, reap_check_results, NULL);


		log_debug_info(DEBUGL_CHECKS, 0, "Starting to reap check results.\n");

		/* process files in the check result queue */
		reaped_checks = process_check_result_queue(check_result_path);

		log_debug_info(DEBUGL_CHECKS, 0, "Finished reaping %d check results\n", reaped_checks);
	}
}

/******************************************************************/
/************************* EVENT CALLBACKS ************************/
/******************************************************************/


/**
 * Parse check output, long output and performance data from a buffer
 * into a struct.
 *
 * @param buf Buffer from which to parse check output
 * @param check_output Where to store the parsed output
 * @return Pointer to the populated check_output struct, or NULL on error
 */
struct check_output *parse_output(const char *buf, struct check_output *check_output) {
	char *saveptr = NULL, *tmpbuf = NULL;
	char *p = NULL, *tmp = NULL;
	dbuf perf_data_dbuf;

	check_output->perf_data = NULL;
	check_output->long_output = NULL;
	check_output->short_output = NULL;
	if(!buf || !*buf)
		return check_output;
	tmpbuf = nm_strdup(buf);

	dbuf_init(&perf_data_dbuf, 1024);
	tmp = strtok_r(tmpbuf, "\n", &saveptr);
	p = strpbrk((const char *) tmp, "|");
	if (p == NULL) {
		/* No perfdata in first line of output. */
			check_output->short_output = tmp ? nm_strdup(tmp) : nm_strdup("");
	}
	else {
		/*
		 * There is perfdata on the first line
		 * the short output consists of the all bytes up
		 * to the perf data delimiter (|), stash those
		 * bytes and add the rest of the string to the
		 * perf data buffer.
		 * */
		if (p!= tmp) {
			check_output->short_output = nm_strndup(tmp, (size_t) (p - tmp));
		}
		else {
			check_output->short_output = nm_strdup("");
		}
		dbuf_strcat(&perf_data_dbuf, tmp+(p-tmp)+1);
	}

	/*
	 * Get the rest of the string, if any.
	 * */
	if ( (tmp = strtok_r(NULL, "", &saveptr)) ) {

		/* Is there a perf data delimiter somewhere in the long output? */
		p = strpbrk((const char *) tmp, "|");
		if (p == NULL) {
			/* No more perfdata, rest is long output*/
			check_output->long_output = nm_strdup(tmp);
		}
		else {
			/* There is perfdata, limit what we regard as long output */
			if (p != tmp) {
				check_output->long_output = nm_strndup(tmp, (size_t) (p - tmp));
			}

			/*
			 * Get rest of string, line by line. This is perfdata if it exists.
			 * This also gets rid of any interleaved newlines in the
			 * perf data - we're not interested in those.
			 * */
			tmp = strtok_r(p+1, "\n", &saveptr);
			while (tmp) {

				/* Backwards compatibility
				 * Each "newline" is padded by a space, if it doesn't
				 * already have such a padding.
				 *
				 * This is a bit silly, since it's not mentioned anywhere
				 * in the documentation as far as I can tell, but I opt to keep
				 * it this way in order to not break existing installations.
				 * */
				if (*tmp != ' ') {
					dbuf_strcat(&perf_data_dbuf, " ");
				}
				dbuf_strcat(&perf_data_dbuf, tmp);
				tmp = strtok_r(NULL, "\n", &saveptr);
			}
		}
	}

	check_output->perf_data = perf_data_dbuf.buf != NULL ? nm_strdup(perf_data_dbuf.buf) : NULL;
	dbuf_free(&perf_data_dbuf);
	free(tmpbuf);
	return check_output;
}

/* parse raw plugin output and return: short and long output, perf data */
int parse_check_output(char *buf, char **short_output, char **long_output, char **perf_data, int escape_newlines_please, int newlines_are_escaped)
{
	struct check_output *check_output = nm_malloc(sizeof(struct check_output));
	check_output = parse_output(buf, check_output);
	*short_output = check_output->short_output;
	*long_output = check_output->long_output;
	*perf_data = check_output->perf_data;
	free(check_output);
	strip(*short_output);
	strip(*perf_data);
	return OK;
}


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
		nm_log(NSLOG_CONFIG_ERROR,
		       "Error: No check result queue directory specified.\n");
		return ERROR;
	}

	/* open the directory for reading */
	if ((dirp = opendir(dirname)) == NULL) {
		nm_log(NSLOG_CONFIG_ERROR,
		       "Error: Could not open check result queue directory '%s' for reading.\n", dirname);
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
				nm_log(NSLOG_RUNTIME_WARNING,
				       "Warning: Could not stat() check result file '%s'.\n", file);
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
			nm_log(NSLOG_RUNTIME_ERROR,
			       "Error: Got check result for service '%s' on host '%s'. Unable to find service\n", cr->service_description, cr->host_name);
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
			nm_log(NSLOG_RUNTIME_ERROR,
			       "Error: Got host checkresult for '%s', but no such host can be found\n", cr->host_name);
			return ERROR;
		}
		log_debug_info(DEBUGL_CHECKS, 2, "Processing check result for host '%s'\n", hst->name);
		hst->check_source = source_name;
		return handle_async_host_check_result(hst, cr);
	}

	/* We should never end up here */
	nm_log(NSLOG_RUNTIME_ERROR,
	       "Error: Unknown object check type for checkresult: %d; (host_name: %s; service_description: %s)\n", cr->object_check_type, cr->host_name ? cr->host_name : "(null)", cr->service_description ? cr->service_description : "(null)");

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
