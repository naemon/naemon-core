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

/*#define DEBUG_CHECKS*/
/*#define DEBUG_HOST_CHECKS 1*/


#ifdef USE_EVENT_BROKER
#include "neberrors.h"
#endif

/* forward declarations */
static void check_orphaned_eventhandler(void *args);
static void reap_check_results(void *arg);


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

	/* add an orphaned check event */
	if (check_orphaned_services == TRUE || check_orphaned_hosts == TRUE) {
		schedule_event(DEFAULT_ORPHAN_CHECK_INTERVAL, check_orphaned_eventhandler, NULL);
	}
}

/******************************************************************/
/********************** CHECK REAPER FUNCTIONS ********************/
/******************************************************************/

static void check_orphaned_eventhandler(void *args)
{
	/* Reschedule, since recurring */
	schedule_event(DEFAULT_ORPHAN_CHECK_INTERVAL, check_orphaned_eventhandler, NULL);

	/* check for orphaned hosts and services */
	if (check_orphaned_hosts == TRUE)
		check_for_orphaned_hosts();
	if (check_orphaned_services == TRUE)
		check_for_orphaned_services();
}

/* reaps host and service check results */
static void reap_check_results(void *arg)
{
	int reaped_checks = 0;
	/* Reschedule, since reccuring */
	schedule_event(check_reaper_interval, reap_check_results, NULL);


	log_debug_info(DEBUGL_FUNCTIONS, 0, "reap_check_results() start\n");
	log_debug_info(DEBUGL_CHECKS, 0, "Starting to reap check results.\n");

	/* process files in the check result queue */
	reaped_checks = process_check_result_queue(check_result_path);

	log_debug_info(DEBUGL_CHECKS, 0, "Finished reaping %d check results\n", reaped_checks);
	log_debug_info(DEBUGL_FUNCTIONS, 0, "reap_check_results() end\n");
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
