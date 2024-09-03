#include "checks.h"
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
#include "objects_hostdependency.h"
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "neberrors.h"

/* Scheduling (before worker job is started) */
static void handle_host_check_event(struct nm_event_execution_properties *evprop);
static int run_async_host_check(host *hst, int check_options, double latency);

/* Result handling (After worker job is executed) */
static void handle_worker_host_check(wproc_result *wpres, void *arg, int flags);
static int process_host_check_result(host *hst, host *pre, int *alert_recorded);
static int handle_host_state(host *hst, int *alert_recorded);

/* Extra features */
static void check_host_result_freshness(struct nm_event_execution_properties *evprop);
static void check_for_orphaned_hosts_eventhandler(struct nm_event_execution_properties *evprop);

/* Status functions, immutable */
static int is_host_result_fresh(host *temp_host, time_t current_time, int log_this);
static int determine_host_reachability(host *hst);

/******************************************************************************
 *******************************  INIT METHODS  *******************************
 ******************************************************************************/

void checks_init_hosts(void)
{
	host *temp_host = NULL;
	time_t delay;
	time_t current_time = time(NULL);

	/******** SCHEDULE HOST CHECKS  ********/

	log_debug_info(DEBUGL_EVENTS, 2, "Scheduling host checks...\n");

	/* add scheduled host checks to event queue */
	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

		/* update status of all hosts (scheduled or not) */
		update_host_status(temp_host, FALSE);

		/* Determine the delay used for the first check event.
		 * If use_retained_scheduling_info is enabled, we use the previously set
		 * next_check. If the check was missed, schedule it within the next
		 * retained_scheduling_randomize_window. If more than one check was missed, we schedule the check
		 * randomly instead. If the next_check is more than one check_interval in
		 * the future, we also schedule the next check randomly. This indicates
		 * that the check_interval has been lowered over restarts.
		 */
		if (use_retained_scheduling_info == TRUE &&
		    temp_host->next_check > current_time - get_host_check_interval_s(temp_host) &&
		    temp_host->next_check <= current_time + get_host_check_interval_s(temp_host)) {
			if (temp_host->next_check < current_time) {
				int scheduling_window = retained_scheduling_randomize_window;
				if (retained_scheduling_randomize_window > get_host_check_interval_s(temp_host)) {
					scheduling_window = get_host_check_interval_s(temp_host);
				}
				delay = ranged_urand(0, scheduling_window);
			} else {
				delay = temp_host->next_check - current_time;
			}
		} else {
			delay = ranged_urand(0, get_host_check_interval_s(temp_host));
		}

		/* schedule a new host check event */
		schedule_next_host_check(temp_host, delay, CHECK_OPTION_NONE);
	}

	/* add a host result "freshness" check event */
	if (check_host_freshness == TRUE) {
		schedule_event(host_freshness_check_interval, check_host_result_freshness, NULL);
	}

	if (check_orphaned_hosts == TRUE) {
		schedule_event(DEFAULT_ORPHAN_CHECK_INTERVAL, check_for_orphaned_hosts_eventhandler, NULL);
	}
}

/******************************************************************************
 ********************************  SCHEDULING  ********************************
 ******************************************************************************/

void schedule_next_host_check(host *hst, time_t delay, int options)
{
	time_t current_time = time(NULL);

	/* A closer check is already scheduled, skip this scheduling */
	if (hst->next_check_event != NULL && hst->next_check < delay + current_time) {
		/*... unless this is a forced check or postponement is allowed*/
		if (!(options & (CHECK_OPTION_FORCE_EXECUTION | CHECK_OPTION_ALLOW_POSTPONE)))
			return;
	}

	/* We have a scheduled check, drop that event to make space for the new event */
	if (hst->next_check_event != NULL) {
		destroy_event(hst->next_check_event);
	}

	/* Schedule the event */
	hst->check_options = options;
	hst->next_check = delay + current_time;
	hst->last_update = current_time;
	hst->next_check_event = schedule_event(delay, handle_host_check_event, (void *)hst);

	/* update the status log, since next_check and check_options is updated */
	update_host_status(hst, FALSE);
}

/* schedules an immediate or delayed host check, DEPRECATED */
void schedule_host_check(host *hst, time_t check_time, int options)
{
	schedule_next_host_check(hst, check_time - time(NULL), options);
}

static void handle_host_check_event(struct nm_event_execution_properties *evprop)
{
	host *hst = (host *)evprop->user_data;
	double latency;
	struct timeval tv;
	int options = hst->check_options;

	int result = OK;

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
		/* get event latency */
		latency = evprop->attributes.timed.latency;
		gettimeofday(&tv, NULL);

		/* When the callback is called, the pointer to the timed event is invalid */
		hst->next_check_event = NULL;

		/*
		 * Reschedule the next check one check interval in the future. Can be
		 * rescheduled to a closer time later if needed (for example, using
		 * retry_interval for problem determination)
		 *
		 * But this is only done if checks are recurring, that is non-zero
		 * check_interval
		 */
		if (hst->check_interval != 0.0 && hst->is_executing == FALSE)
			schedule_next_host_check(hst, get_host_check_interval_s(hst), CHECK_OPTION_NONE);

		/* Don't run checks if checks are disabled, unless foreced */
		if (execute_host_checks == FALSE && !(options & CHECK_OPTION_FORCE_EXECUTION)) {
			return;
		}

		/* Time to run event */
		log_debug_info(DEBUGL_CHECKS, 0, "Attempting to run scheduled check of host '%s': check options=%d, latency=%lf\n", hst->name, options, latency);

		/* attempt to run the check */
		result = run_async_host_check(hst, options, latency);

		/* an error occurred, so reschedule the check */
		if (result == ERROR) {
			/* Somethings wrong, reschedule for retry interval instead, if retry_interval is specified. */
			if (hst->retry_interval != 0.0 && hst->is_executing == FALSE) {
				schedule_next_host_check(hst, get_host_retry_interval_s(hst), CHECK_OPTION_NONE);
				log_debug_info(DEBUGL_CHECKS, 1, "Rescheduled next host check for %s\n", ctime(&hst->next_check));
			}

			/* update the status log */
			update_host_status(hst, FALSE);
		}
	} else if (evprop->execution_type == EVENT_EXEC_ABORTED) {
		/* If the event is destroyed, remove the reference. */
		hst->next_check_event = NULL;
	}
}

/******************************************************************************
 *****************************  CHECK EXECUTION  ******************************
 ******************************************************************************/

/* perform an asynchronous check of a host */
/* scheduled host checks will use this, as will some checks that result from on-demand checks... */
static int run_async_host_check(host *hst, int check_options, double latency)
{
	nagios_macros mac;
	char *raw_command = NULL;
	char *processed_command = NULL;
	struct timeval start_time, end_time;
	check_result *cr;
	int runchk_result = OK;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	int neb_result = OK;
	time_t now = time(NULL);

	log_debug_info(DEBUGL_CHECKS, 0, "** Running async check of host '%s'...\n", hst->name);

	/*
	 * Check for reasons not to even try to execute this check.
	 *
	 * But if the check is forced, we should check it anyway.
	 */
	if (!(check_options & CHECK_OPTION_FORCE_EXECUTION)) {
		/* abort if check is already running */
		if (hst->is_executing == TRUE) {
			log_debug_info(DEBUGL_CHECKS, 1, "A check of this host is already being executed, so we'll pass for the moment...\n");
			return ERROR;
		}

		/* abort if check was recently completed */
		if (hst->last_check + cached_host_check_horizon > now && hst->last_check <= now) {
			log_debug_info(DEBUGL_CHECKS, 0, "Host '%s' was last checked within its cache horizon. Aborting check\n", hst->name);
			return ERROR;
		}

		/* if checks of the host are currently disabled... */
		if (hst->checks_enabled == FALSE) {
			return ERROR;
		}

		/* make sure this is a valid time to check the host */
		if (check_time_against_period(time(NULL), hst->check_period_ptr) != OK) {
			return ERROR;
		}

		/* check host dependencies for execution */
		log_debug_info(DEBUGL_CHECKS, 0, "Host '%s' checking dependencies...\n", hst->name);
		if (check_host_dependencies(hst, EXECUTION_DEPENDENCY) == DEPENDENCIES_FAILED) {
			int keep_running = FALSE;
			switch(host_skip_check_dependency_status) {
				case SKIP_KEEP_RUNNING_WHEN_UP:
					if (hst->current_state == STATE_UP) {
						keep_running = TRUE;
					}
					break;
				case STATE_UP:
				case STATE_DOWN:
				case STATE_UNREACHABLE:
					hst->current_state = host_skip_check_dependency_status;
					if (strstr(hst->plugin_output, "(host dependency check failed)") == NULL) {
						char *old_output = nm_strdup(hst->plugin_output);
						nm_free(hst->plugin_output);
						nm_asprintf(&hst->plugin_output, "(host dependency check failed) was: %s", old_output);
						nm_free(old_output);
					}
					break;
			}
			if(!keep_running) {
				log_debug_info(DEBUGL_CHECKS, 0, "Host '%s' failed dependency check. Aborting check\n", hst->name);
				return ERROR;
			}
		}
	}

	/******** GOOD TO GO FOR A REAL HOST CHECK AT THIS POINT ********/

	/* initialize start/end times */
	start_time.tv_sec = 0L;
	start_time.tv_usec = 0L;
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	hst->last_update = now;

	neb_result = broker_host_check(NEBTYPE_HOSTCHECK_ASYNC_PRECHECK, NEBFLAG_NONE, NEBATTR_NONE, hst, CHECK_TYPE_ACTIVE, hst->current_state, hst->state_type, start_time, end_time, hst->check_command, hst->latency, 0.0, host_check_timeout, FALSE, 0, NULL, NULL, NULL, NULL, NULL);

	if (neb_result == NEBERROR_CALLBACKCANCEL || neb_result == NEBERROR_CALLBACKOVERRIDE) {
		log_debug_info(DEBUGL_CHECKS, 0, "Check of host '%s' (id=%u) was %s by a module\n",
		               hst->name, hst->id,
		               neb_result == NEBERROR_CALLBACKCANCEL ? "cancelled" : "overridden");
	}
	/* neb module wants to cancel the host check - the check will be rescheduled for a later time by the scheduling logic */
	if (neb_result == NEBERROR_CALLBACKCANCEL) {
		return ERROR;
	}

	/* neb module wants to override the host check - perhaps it will check the host itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE)
		return OK;

	log_debug_info(DEBUGL_CHECKS, 0, "Checking host '%s'...\n", hst->name);

	/* set latency for macros and event broker */
	hst->latency = latency;

	/* grab the host macro variables */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, hst);

	get_raw_command_line_r(&mac, hst->check_command_ptr, hst->check_command, &raw_command, macro_options);
	if (raw_command == NULL) {
		clear_volatile_macros_r(&mac);
		log_debug_info(DEBUGL_CHECKS, 0, "Raw check command for host '%s' was NULL - aborting.\n", hst->name);
		return ERROR;
	}

	/* process any macros contained in the argument */
	process_macros_r(&mac, raw_command, &processed_command, macro_options);
	nm_free(raw_command);
	if (processed_command == NULL) {
		clear_volatile_macros_r(&mac);
		log_debug_info(DEBUGL_CHECKS, 0, "Processed check command for host '%s' was NULL - aborting.\n", hst->name);
		return ERROR;
	}

	/* get the command start time */
	gettimeofday(&start_time, NULL);

	cr = nm_calloc(1, sizeof(*cr));
	init_check_result(cr);

	/* save check info */
	cr->object_check_type = HOST_CHECK;
	cr->host_name = nm_strdup(hst->name);
	cr->service_description = NULL;
	cr->check_type = CHECK_TYPE_ACTIVE;
	cr->check_options = check_options;
	cr->scheduled_check = TRUE;
	cr->latency = latency;
	cr->start_time = start_time;
	cr->finish_time = start_time;
	cr->early_timeout = FALSE;
	cr->exited_ok = TRUE;
	cr->return_code = STATE_OK;
	cr->output = NULL;

	neb_result = broker_host_check(NEBTYPE_HOSTCHECK_INITIATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CHECK_TYPE_ACTIVE, hst->current_state, hst->state_type, start_time, end_time, hst->check_command, hst->latency, 0.0, host_check_timeout, FALSE, 0, processed_command, NULL, NULL, NULL, cr);

	/* neb module wants to override the service check - perhaps it will check the service itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE || neb_result == NEBERROR_CALLBACKCANCEL) {
		clear_volatile_macros_r(&mac);
		free_check_result(cr);
		nm_free(cr);
		nm_free(processed_command);
		return neb_result == NEBERROR_CALLBACKOVERRIDE ? OK : ERROR;
	}

	runchk_result = wproc_run_callback(processed_command, host_check_timeout, handle_worker_host_check, (void *)cr, &mac);
	if (runchk_result == ERROR) {
		nm_log(NSLOG_RUNTIME_ERROR,
		       "Unable to send check for host '%s' to worker (ret=%d)\n", hst->name, runchk_result);
	} else {
		/* do the book-keeping */
		currently_running_host_checks++;
		hst->is_executing = TRUE;
		update_check_stats(ACTIVE_SCHEDULED_HOST_CHECK_STATS, start_time.tv_sec);
		update_check_stats(PARALLEL_HOST_CHECK_STATS, start_time.tv_sec);
	}


	clear_volatile_macros_r(&mac);
	nm_free(processed_command);

	return OK;
}

/******************************************************************************
 *****************************  RESULT HANDLING  ******************************
 ******************************************************************************/

/*
 * Update host state with check result info
 * This function causes no sideeffect actions, such as state
 * logging, execution of event handlers or notifications or
 * rescheduling of the check
 */
int update_host_state_post_check(struct host *hst, struct check_result *cr)
{
	int result;
	char *temp_ptr = NULL;
	time_t now = time(NULL);

	if (!hst || !cr)
		return ERROR;

	log_debug_info(DEBUGL_CHECKS, 1, "** Handling check result for host '%s' from '%s'...\n", hst->name, check_result_source(cr));
	log_debug_info(DEBUGL_CHECKS, 2, "\tCheck Type:         %s\n", (cr->check_type == CHECK_TYPE_ACTIVE) ? "Active" : "Passive");
	log_debug_info(DEBUGL_CHECKS, 2, "\tCheck Options:      %d\n", cr->check_options);
	log_debug_info(DEBUGL_CHECKS, 2, "\tScheduled Check?:   %s\n", (cr->scheduled_check == TRUE) ? "Yes" : "No");
	log_debug_info(DEBUGL_CHECKS, 2, "\tExited OK?:         %s\n", (cr->exited_ok == TRUE) ? "Yes" : "No");
	log_debug_info(DEBUGL_CHECKS, 2, "\tExec Time:          %.3f\n", hst->execution_time);
	log_debug_info(DEBUGL_CHECKS, 2, "\tLatency:            %.3f\n", hst->latency);
	log_debug_info(DEBUGL_CHECKS, 2, "\tReturn Status:      %d\n", cr->return_code);
	log_debug_info(DEBUGL_CHECKS, 2, "\tOutput:             %s\n", (cr == NULL) ? "NULL" : cr->output);

	/* clear the freshening flag (it would have been set if this host was determined to be stale) */
	if (cr->check_options & CHECK_OPTION_FRESHNESS_CHECK)
		hst->is_being_freshened = FALSE;

	/*
	 * Only accept freshness checks if host is still stale, or
	 * we might end up overwriting a passive result that came in after
	 * the freshness check was triggered but before the freshness
	 * check completed
	 */
	if ((cr->check_options & CHECK_OPTION_FRESHNESS_CHECK) && is_host_result_fresh(hst, cr->finish_time.tv_sec, FALSE) == TRUE) {
		log_debug_info(DEBUGL_CHECKS, 0, "Discarding host freshness check result because the host is currently fresh (race condition avoided).\n");
		return OK;
	}

	hst->latency = cr->latency;
	hst->check_type = cr->check_type;
	hst->has_been_checked = TRUE;

	/* update the execution time for this check (millisecond resolution) */
	hst->execution_time = (double)((double)(cr->finish_time.tv_sec - cr->start_time.tv_sec) + (double)((cr->finish_time.tv_usec - cr->start_time.tv_usec) / 1000.0) / 1000.0);
	if (hst->execution_time < 0.0)
		hst->execution_time = 0.0;

	/* get the last check time */
	hst->last_check = cr->start_time.tv_sec;
	hst->last_update = now;

	/* save the old host state */
	hst->last_state = hst->current_state;
	if (hst->state_type == HARD_STATE)
		hst->last_hard_state = hst->current_state;

	/* clear the old plugin output and perf data buffers */
	nm_free(hst->plugin_output);
	nm_free(hst->long_plugin_output);
	nm_free(hst->perf_data);

	/* parse check output to get: (1) short output, (2) long output, (3) perf data */
	parse_check_output(cr->output, &hst->plugin_output, &hst->long_plugin_output, &hst->perf_data, TRUE, FALSE);

	/* make sure we have some data */
	if (hst->plugin_output == NULL) {
		hst->plugin_output = nm_strdup("(No output returned from host check)");
	}

	/* replace semicolons in plugin output (but not performance data) with colons */
	if ((temp_ptr = hst->plugin_output)) {
		while ((temp_ptr = strchr(temp_ptr, ';')))
			* temp_ptr = ':';
	}

	log_debug_info(DEBUGL_CHECKS, 2, "Parsing check output...\n");
	log_debug_info(DEBUGL_CHECKS, 2, "Short Output: %s\n", (hst->plugin_output == NULL) ? "NULL" : hst->plugin_output);
	log_debug_info(DEBUGL_CHECKS, 2, "Long Output:  %s\n", (hst->long_plugin_output == NULL) ? "NULL" : hst->long_plugin_output);
	log_debug_info(DEBUGL_CHECKS, 2, "Perf Data:    %s\n", (hst->perf_data == NULL) ? "NULL" : hst->perf_data);

	/* get the unprocessed return code */
	/* NOTE: for passive checks, this is the final/processed state */
	result = cr->return_code;

	/* adjust return code (active checks only) */
	if (cr->check_type == CHECK_TYPE_ACTIVE) {
		hst->is_executing = FALSE;

		if (cr->early_timeout) {
			nm_free(hst->plugin_output);
			nm_free(hst->long_plugin_output);
			nm_free(hst->perf_data);
			nm_asprintf(&hst->plugin_output, "(Host check timed out after %.2lf seconds)", hst->execution_time);
			result = STATE_UNKNOWN;
		}

		/* if there was some error running the command, just skip it */
		else if (cr->exited_ok == FALSE) {

			nm_log(NSLOG_RUNTIME_WARNING,
			       "Warning:  Check of host '%s' did not exit properly!\n", hst->name);

			nm_free(hst->plugin_output);
			nm_free(hst->long_plugin_output);
			nm_free(hst->perf_data);
			hst->plugin_output = nm_strdup("(Host check did not exit properly)");

			result = STATE_CRITICAL;
		}

		/* make sure the return code is within bounds */
		else if (cr->return_code < 0 || cr->return_code > 3) {

			nm_log(NSLOG_RUNTIME_WARNING,
			       "Warning: Return code of %d for check of host '%s' was out of bounds.%s\n", cr->return_code, hst->name, (cr->return_code == 126 || cr->return_code == 127) ? " Make sure the plugin you're trying to run actually exists." : "");

			nm_free(hst->plugin_output);
			nm_free(hst->long_plugin_output);
			nm_free(hst->perf_data);
			nm_asprintf(&hst->plugin_output, "(Return code of %d is out of bounds%s)", cr->return_code, (cr->return_code == 126 || cr->return_code == 127) ? " - plugin may be missing" : "");

			result = STATE_CRITICAL;
		}

		/* a NULL host check command means we should assume the host is UP */
		if (hst->check_command == NULL) {
			nm_free(hst->plugin_output);
			hst->plugin_output = nm_strdup("(Host assumed to be UP)");
			result = STATE_OK;
		}
	}

	/* translate return code to basic UP/DOWN state - the DOWN/UNREACHABLE state determination is made later */
	/* NOTE: only do this for active checks - passive check results already have the final state */
	if (cr->check_type == CHECK_TYPE_ACTIVE) {
		/*
		 * if we're not doing aggressive host checking, let WARNING
		 * states indicate the host is up
		 */
		if (use_aggressive_host_checking == FALSE && result == STATE_WARNING)
			result = STATE_OK;

		/* OK states means the host is UP */
		if (result == STATE_OK)
			result = STATE_UP;

		/* any problem state indicates the host is not UP */
		else
			result = STATE_DOWN;
	}

	/* we have to adjust current attempt # for passive checks, as it isn't done elsewhere */
	if (hst->check_type == CHECK_TYPE_PASSIVE) {
		if (passive_host_checks_are_soft == TRUE)
			adjust_host_check_attempt(hst, FALSE);
		else
			hst->state_type = HARD_STATE;
	} else if (hst->check_type == CHECK_TYPE_ACTIVE)
		adjust_host_check_attempt(hst, TRUE);

	if (hst->current_attempt >= hst->max_attempts)
		hst->state_type = HARD_STATE;
	hst->current_state = result;

	/* record the time the last state ended */
	switch (hst->last_state) {
	case STATE_UP:
		hst->last_time_up = cr->finish_time.tv_sec;
		break;
	case STATE_DOWN:
		hst->last_time_down = cr->finish_time.tv_sec;
		break;
	case STATE_UNREACHABLE:
		hst->last_time_unreachable = cr->finish_time.tv_sec;
		break;
	default:
		break;
	}

	return 0;
}

/* process results of an asynchronous host check */
int handle_async_host_check_result(host *temp_host, check_result *cr)
{
	int alert_recorded = NEBATTR_NONE;
	struct timeval end_time_hires;
	struct host pre;
	int first_recorded_state = NEBATTR_NONE;

	/* make sure we have what we need */
	if (temp_host == NULL || cr == NULL)
		return ERROR;

	/* skip this host check results if its passive and we aren't accepting passive check results */
	if (cr->check_type == CHECK_TYPE_PASSIVE) {
		if (accept_passive_host_checks == FALSE) {
			log_debug_info(DEBUGL_CHECKS, 0, "Discarding passive host check result because passive host checks are disabled globally.\n");
			return ERROR;
		}
		if (temp_host->accept_passive_checks == FALSE) {
			log_debug_info(DEBUGL_CHECKS, 0, "Discarding passive host check result because passive checks are disabled for this host.\n");
			return ERROR;
		}
	}

	/* update check statistics for passive results */
	if (cr->check_type == CHECK_TYPE_PASSIVE)
		update_check_stats(PASSIVE_HOST_CHECK_STATS, cr->start_time.tv_sec);

	if (!temp_host->last_check)
		first_recorded_state = NEBATTR_CHECK_FIRST;

	memcpy(&pre, temp_host, sizeof(pre));
	if (temp_host->plugin_output)
		pre.plugin_output = nm_strdup(temp_host->plugin_output);
	if (temp_host->long_plugin_output)
		pre.long_plugin_output = nm_strdup(temp_host->long_plugin_output);
	if (temp_host->perf_data)
		pre.perf_data = nm_strdup(temp_host->perf_data);

	/******************* PROCESS THE CHECK RESULTS ******************/
	update_host_state_post_check(temp_host, cr);

	/* process the host check result */
	process_host_check_result(temp_host, &pre, &alert_recorded);

	nm_free(pre.plugin_output);
	nm_free(pre.long_plugin_output);
	nm_free(pre.perf_data);

	log_debug_info(DEBUGL_CHECKS, 1, "** Async check result for host '%s' handled: new state=%d\n", temp_host->name, temp_host->current_state);

	/* high resolution end time for event broker */
	gettimeofday(&end_time_hires, NULL);

	broker_host_check(
	    NEBTYPE_HOSTCHECK_PROCESSED,
	    NEBFLAG_NONE,
	    alert_recorded | first_recorded_state,
	    temp_host,
	    temp_host->check_type,
	    temp_host->current_state,
	    temp_host->state_type,
	    cr->start_time,
	    cr->finish_time,
	    temp_host->check_command,
	    temp_host->latency,
	    temp_host->execution_time,
	    host_check_timeout,
	    cr->early_timeout,
	    cr->return_code,
	    NULL,
	    temp_host->plugin_output,
	    temp_host->long_plugin_output,
	    temp_host->perf_data,
	    cr);

	temp_host->last_update = end_time_hires.tv_sec;
	return OK;
}

static void handle_worker_host_check(wproc_result *wpres, void *arg, int flags)
{
	check_result *cr = (check_result *)arg;
	struct host *hst;
	time_t now = time(NULL);

	/* decrement the number of host checks still out there... */
	if (currently_running_host_checks > 0)
		currently_running_host_checks--;

	if (wpres) {
		hst = find_host(cr->host_name);
		if (hst) {
			hst->is_executing = FALSE;
			hst->last_update = now;
			memcpy(&cr->rusage, &wpres->rusage, sizeof(wpres->rusage));
			cr->start_time.tv_sec = wpres->start.tv_sec;
			cr->start_time.tv_usec = wpres->start.tv_usec;
			cr->finish_time.tv_sec = wpres->stop.tv_sec;
			cr->finish_time.tv_usec = wpres->stop.tv_usec;
			if (WIFEXITED(wpres->wait_status)) {
				cr->return_code = WEXITSTATUS(wpres->wait_status);
			} else {
				cr->return_code = STATE_UNKNOWN;
			}

			if (wpres->outstd && *wpres->outstd) {
				cr->output = nm_strdup(wpres->outstd);
			} else if (wpres->outerr && *wpres->outerr) {
				nm_asprintf(&cr->output, "(No output on stdout) stderr: %s", wpres->outerr);
			} else {
				cr->output = NULL;
			}

			cr->early_timeout = wpres->early_timeout;
			cr->exited_ok = wpres->exited_ok;
			cr->engine = NULL;
			cr->source = wpres->source;
			process_check_result(cr);
		}
	}
	free_check_result(cr);
	nm_free(cr);
}

static gboolean propagate_when_not_up(gpointer _name, gpointer _hst, gpointer user_data)
{
	host *hst = (host *)_hst;
	char *direction = (char *)user_data;
	if (hst->current_state != STATE_UP) {
		schedule_next_host_check(hst, 0, CHECK_OPTION_NONE);
		log_debug_info(DEBUGL_CHECKS, 1, "Check of %s host '%s' queued.\n", direction, hst->name);
	}
	return FALSE;
}

static gboolean propagate_when_up(gpointer _name, gpointer _hst, gpointer user_data)
{
	host *hst = (host *)_hst;
	char *direction = (char *)user_data;
	if (hst->current_state == STATE_UP) {
		schedule_next_host_check(hst, 0, CHECK_OPTION_NONE);
		log_debug_info(DEBUGL_CHECKS, 1, "Check of %s host '%s' queued.\n", direction, hst->name);
	}
	return FALSE;
}

static gboolean propagate_when_not_unreachable(gpointer _name, gpointer _hst, gpointer user_data)
{
	host *hst = (host *)_hst;
	char *direction = (char *)user_data;
	if (hst->current_state != STATE_UNREACHABLE) {
		schedule_next_host_check(hst, 0, CHECK_OPTION_NONE);
		log_debug_info(DEBUGL_CHECKS, 1, "Check of %s host '%s' queued.\n", direction, hst->name);
	}
	return FALSE;
}


/* processes the result of a synchronous or asynchronous host check */
static int process_host_check_result(host *hst, host *prev, int *alert_recorded)
{
	host *master_host = NULL;
	time_t current_time = 0L;

	log_debug_info(DEBUGL_CHECKS, 1, "HOST: %s, ATTEMPT=%d/%d, CHECK TYPE=%s, STATE TYPE=%s, OLD STATE=%d, NEW STATE=%d\n", hst->name, hst->current_attempt, hst->max_attempts, (hst->check_type == CHECK_TYPE_ACTIVE) ? "ACTIVE" : "PASSIVE", (hst->state_type == HARD_STATE) ? "HARD" : "SOFT", hst->current_state, hst->current_state);

	/* get the current time */
	time(&current_time);

	/* log passive checks - we need to do this here, as some my bypass external commands by getting dropped in checkresults dir */
	if (hst->check_type == CHECK_TYPE_PASSIVE) {
		if (log_passive_checks == TRUE)
			nm_log(NSLOG_PASSIVE_CHECK,
			       "PASSIVE HOST CHECK: %s;%d;%s\n", hst->name, hst->current_state, hst->plugin_output);
	}


	/******* HOST WAS DOWN/UNREACHABLE INITIALLY *******/
	if (prev->current_state != STATE_UP) {

		log_debug_info(DEBUGL_CHECKS, 1, "Host was %s.\n", host_state_name(prev->current_state));

		/***** HOST IS NOW UP *****/
		/* the host just recovered! */
		if (hst->current_state == STATE_UP) {

			/* set the current state */
			hst->current_state = STATE_UP;

			/* set the state type */
			/* set state type to HARD for passive checks and active checks that were previously in a HARD STATE */
			if (hst->state_type == HARD_STATE || (hst->check_type == CHECK_TYPE_PASSIVE && passive_host_checks_are_soft == FALSE))
				hst->state_type = HARD_STATE;
			else
				hst->state_type = SOFT_STATE;

			log_debug_info(DEBUGL_CHECKS, 1, "Host experienced a %s recovery (it's now UP).\n", (hst->state_type == HARD_STATE) ? "HARD" : "SOFT");

			/* propagate checks to immediate parents if they are not already UP */
			/* we do this because a parent host (or grandparent) may have recovered somewhere and we should catch the recovery as soon as possible */
			log_debug_info(DEBUGL_CHECKS, 1, "Propagating checks to parent host(s)...\n");
			g_tree_foreach(hst->parent_hosts, propagate_when_not_up, "parent");

			/* propagate checks to immediate children if they are not already UP */
			/* we do this because children may currently be UNREACHABLE, but may (as a result of this recovery) switch to UP or DOWN states */
			log_debug_info(DEBUGL_CHECKS, 1, "Propagating checks to child host(s)...\n");
			g_tree_foreach(hst->child_hosts, propagate_when_not_up, "child");
		}

		/***** HOST IS STILL DOWN/UNREACHABLE *****/
		/* we're still in a problem state... */
		else {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is still %s.\n", host_state_name(hst->current_state));

			/* passive checks are treated as HARD states by default... */
			if (hst->check_type == CHECK_TYPE_PASSIVE && passive_host_checks_are_soft == FALSE) {

				/* set the state type */
				hst->state_type = HARD_STATE;

				/* reset the current attempt */
				hst->current_attempt = 1;
			}

			/* active checks and passive checks (treated as SOFT states) */
			else {

				/* set the state type */
				/* we've maxed out on the retries */
				if (hst->current_attempt == hst->max_attempts)
					hst->state_type = HARD_STATE;
				/* the host was in a hard problem state before, so it still is now */
				else if (hst->current_attempt == 1)
					hst->state_type = HARD_STATE;
				/* the host is in a soft state and the check will be retried */
				else
					hst->state_type = SOFT_STATE;
			}

			/* make a determination of the host's state */
			/* translate host state between DOWN/UNREACHABLE (only for passive checks if enabled) */
			if (hst->check_type == CHECK_TYPE_ACTIVE || translate_passive_host_checks == TRUE)
				hst->current_state = determine_host_reachability(hst);
		}
	}

	/******* HOST WAS UP INITIALLY *******/
	else {

		log_debug_info(DEBUGL_CHECKS, 1, "Host was UP.\n");

		/***** HOST IS STILL UP *****/
		/* either the host never went down since last check */
		if (hst->current_state == STATE_UP) {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is still UP.\n");

			/* set current state and state type */
			hst->state_type = HARD_STATE;
		}

		/***** HOST IS NOW DOWN/UNREACHABLE *****/
		else {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is now %s.\n", host_state_name(hst->current_state));

			/* active and (in some cases) passive check results are treated as SOFT states */
			if (hst->check_type == CHECK_TYPE_ACTIVE || passive_host_checks_are_soft == TRUE) {

				/* set the state type */
				if (hst->current_attempt == hst->max_attempts)
					hst->state_type = HARD_STATE;
				else
					hst->state_type = SOFT_STATE;
			}

			/* by default, passive check results are treated as HARD states */
			else {

				/* set the state type */
				hst->state_type = HARD_STATE;

				/* reset the current attempt */
				hst->current_attempt = 1;
			}

			/* make a (in some cases) preliminary determination of the host's state */
			/* translate host state between DOWN/UNREACHABLE (for passive checks only if enabled) */
			if (hst->check_type == CHECK_TYPE_ACTIVE || translate_passive_host_checks == TRUE)
				hst->current_state = determine_host_reachability(hst);

			/* propagate checks to immediate parents if they are UP */
			/* we do this because a parent host (or grandparent) may have gone down and blocked our route */
			/* checking the parents ASAP will allow us to better determine the final state (DOWN/UNREACHABLE) of this host later */
			log_debug_info(DEBUGL_CHECKS, 1, "Propagating checks to immediate parent hosts that are UP...\n");
			g_tree_foreach(hst->parent_hosts, propagate_when_up, "parent");

			/* propagate checks to immediate children if they are not UNREACHABLE */
			/* we do this because we may now be blocking the route to child hosts */
			log_debug_info(DEBUGL_CHECKS, 1, "Propagating checks to immediate non-UNREACHABLE child hosts...\n");
			g_tree_foreach(hst->child_hosts, propagate_when_not_unreachable, "child");

			/* check dependencies on second to last host check */
			if (enable_predictive_host_dependency_checks == TRUE && hst->current_attempt == (hst->max_attempts - 1)) {
				objectlist *list;

				/* propagate checks to hosts that THIS ONE depends on for notifications AND execution */
				/* we do to help ensure that the dependency checks are accurate before it comes time to notify */
				log_debug_info(DEBUGL_CHECKS, 1, "Propagating predictive dependency checks to hosts this one depends on...\n");

				for (list = hst->notify_deps; list; list = list->next) {
					hostdependency *dep = (hostdependency *)list->object_ptr;
					if (dep->dependent_host_ptr == hst && dep->master_host_ptr != NULL) {
						master_host = (host *)dep->master_host_ptr;
						log_debug_info(DEBUGL_CHECKS, 1, "Check of host '%s' queued.\n", master_host->name);
						schedule_next_host_check(master_host, 0, CHECK_OPTION_NONE);
					}
				}
				for (list = hst->exec_deps; list; list = list->next) {
					hostdependency *dep = (hostdependency *)list->object_ptr;
					if (dep->dependent_host_ptr == hst && dep->master_host_ptr != NULL) {
						master_host = (host *)dep->master_host_ptr;
						log_debug_info(DEBUGL_CHECKS, 1, "Check of host '%s' queued.\n", master_host->name);
						schedule_next_host_check(master_host, 0, CHECK_OPTION_NONE);
					}
				}
			}
		}
	}

	log_debug_info(DEBUGL_CHECKS, 1, "Pre-handle_host_state() Host: %s, Attempt=%d/%d, Type=%s, Final State=%d (%s)\n", hst->name, hst->current_attempt, hst->max_attempts, (hst->state_type == HARD_STATE) ? "HARD" : "SOFT", hst->current_state, host_state_name(hst->current_state));

	/* handle the host state */
	handle_host_state(hst, alert_recorded);

	log_debug_info(DEBUGL_CHECKS, 1, "Post-handle_host_state() Host: %s, Attempt=%d/%d, Type=%s, Final State=%d (%s)\n", hst->name, hst->current_attempt, hst->max_attempts, (hst->state_type == HARD_STATE) ? "HARD" : "SOFT", hst->current_state, host_state_name(hst->current_state));


	/******************** POST-PROCESSING STUFF *********************/

	/* if the plugin output differs from previous check and no state change, log the current state/output if state stalking is enabled */
	if (hst->last_state == hst->current_state && should_stalk(hst) && (g_strcmp0(prev->plugin_output, hst->plugin_output) || g_strcmp0(prev->long_plugin_output, hst->long_plugin_output))) {
		log_host_event(hst);
		*alert_recorded = NEBATTR_CHECK_ALERT;
	}

	/* check to see if the associated host is flapping */
	check_for_host_flapping(hst, TRUE, TRUE);

	/* If there is a problem, and the state still is soft, use retry interval  */
	if (hst->current_state != STATE_UP && hst->state_type == SOFT_STATE) {
		if (hst->retry_interval != 0.0) {
			/* respect retry interval even if an earlier check is scheduled */
			schedule_next_host_check(hst,
			                         get_host_retry_interval_s(hst),
			                         CHECK_OPTION_ALLOW_POSTPONE);
		}
	}

	/* make sure there is a next check event scheduled */
	if (hst->check_interval != 0.0 && hst->next_check_event == NULL) {
		schedule_next_host_check(hst, get_host_check_interval_s(hst), CHECK_OPTION_NONE);
	}

	/* update host status - for both active (scheduled) and passive (non-scheduled) hosts */
	update_host_status(hst, FALSE);

	return OK;
}

/* adjusts current host check attempt when a check is processed */
int adjust_host_check_attempt(host *hst, int is_active)
{
	log_debug_info(DEBUGL_CHECKS, 2, "Adjusting check attempt number for host '%s': current attempt=%d/%d, state=%d, state type=%d\n", hst->name, hst->current_attempt, hst->max_attempts, hst->current_state, hst->state_type);

	/* if host is in a hard state, reset current attempt number */
	if (hst->state_type == HARD_STATE)
		hst->current_attempt = 1;

	/* if host is in a soft UP state, reset current attempt number (active checks only) */
	else if (is_active == TRUE && hst->state_type == SOFT_STATE && hst->current_state == STATE_UP)
		hst->current_attempt = 1;

	/* increment current attempt number */
	else if (hst->current_attempt < hst->max_attempts)
		hst->current_attempt++;

	log_debug_info(DEBUGL_CHECKS, 2, "New check attempt number = %d\n", hst->current_attempt);

	return OK;
}

/* top level host state handler - occurs after every host check (soft/hard and active/passive) */
static int handle_host_state(host *hst, int *alert_recorded)
{
	int state_change = FALSE;
	int hard_state_change = FALSE;
	time_t current_time = 0L;

	/* get current time */
	time(&current_time);

	/* obsess over this host check */
	obsessive_compulsive_host_check_processor(hst);

	/* update performance data */
	update_host_performance_data(hst);

	/* has the host state changed? */
	if (hst->last_state != hst->current_state || (hst->current_state == STATE_UP && hst->state_type == SOFT_STATE))
		state_change = TRUE;

	if (hst->current_attempt >= hst->max_attempts && hst->last_hard_state != hst->current_state)
		hard_state_change = TRUE;

	/* if the host state has changed... */
	if (state_change == TRUE || hard_state_change == TRUE) {

		/* reset the next and last notification times */
		hst->last_notification = (time_t)0;
		hst->next_notification = (time_t)0;

		/* reset notification suppression option */
		hst->no_more_notifications = FALSE;

		/* reset the acknowledgement flag if necessary */
		if (hst->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL && (state_change == TRUE || hard_state_change == FALSE)) {

			hst->problem_has_been_acknowledged = FALSE;
			hst->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
			hst->acknowledgement_end_time = (time_t)0;

			/* remove any non-persistant comments associated with the ack */
			delete_host_acknowledgement_comments(hst);
		} else if (hst->acknowledgement_type == ACKNOWLEDGEMENT_STICKY && hst->current_state == STATE_UP) {

			hst->problem_has_been_acknowledged = FALSE;
			hst->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
			hst->acknowledgement_end_time = (time_t)0;

			/* remove any non-persistant comments associated with the ack */
			delete_host_acknowledgement_comments(hst);
		}

	}

	/* Not sure about this, but is old behaviour */
	if (hst->last_hard_state != hst->current_state)
		hard_state_change = TRUE;

	if (state_change == TRUE || hard_state_change == TRUE) {
		int num_downtimes_start = 0;

		/* update last state change times */
		hst->last_state_change = current_time;
		if (hst->state_type == HARD_STATE)
			hst->last_hard_state_change = current_time;

		/* update the event id */
		hst->last_event_id = hst->current_event_id;
		hst->current_event_id = next_event_id;
		next_event_id++;

		/* update the problem id when transitioning to a problem state */
		if (hst->last_state == STATE_UP) {
			/* don't reset last problem id, or it will be zero the next time a problem is encountered */
			nm_free(hst->current_problem_id);
			hst->current_problem_id = (char*)g_uuid_string_random();
			hst->problem_start = current_time;
			hst->problem_end = 0L;
		}

		/* clear the problem id when transitioning from a problem state to an UP state */
		if (hst->current_state == STATE_UP) {
			hst->last_problem_id = hst->current_problem_id;
			hst->current_problem_id = NULL;
			if(hst->problem_start > 0)
				hst->problem_end = current_time;
		}

		/* write the host state change to the main log file */
		if (hst->state_type == HARD_STATE || (hst->state_type == SOFT_STATE && log_host_retries == TRUE)) {
			log_host_event(hst);
			*alert_recorded = NEBATTR_CHECK_ALERT;
		}

		/* check for start of flexible (non-fixed) scheduled downtime */
		/* It can start on soft states */
		num_downtimes_start = check_pending_flex_host_downtime(hst);

		/* notify contacts about the recovery or problem if its a "hard" state */
		/* And only if no downtime is being started */
		if (hst->state_type == HARD_STATE && num_downtimes_start == 0)
			host_notification(hst, NOTIFICATION_NORMAL, NULL, NULL, NOTIFICATION_OPTION_NONE);

		/* handle the host state change */
		handle_host_event(hst);

		/* the host just recovered, so reset the current host attempt */
		if (hst->current_state == STATE_UP)
			hst->current_attempt = 1;

		/* the host recovered, so reset the current notification number and state flags (after the recovery notification has gone out) */
		if (hst->current_state == STATE_UP) {
			hst->current_notification_number = 0;
			hst->notified_on = 0;
		}
	}

	/* else the host state has not changed */
	else {

		/* notify contacts if host is still down or unreachable */
		if (hst->current_state != STATE_UP && hst->state_type == HARD_STATE)
			host_notification(hst, NOTIFICATION_NORMAL, NULL, NULL, NOTIFICATION_OPTION_NONE);

		/* if we're in a soft state and we should log host retries, do so now... */
		if (hst->state_type == SOFT_STATE && log_host_retries == TRUE) {
			log_host_event(hst);
			*alert_recorded = NEBATTR_CHECK_ALERT;
		}
	}

	/* initialize the last host state change times if necessary */
	if (hst->last_state_change == (time_t)0)
		hst->last_state_change = hst->last_check;
	if (hst->last_hard_state_change == (time_t)0)
		hst->last_hard_state_change = hst->last_check;

	return OK;
}


/******************************************************************************
 ******************************  EXTRA FEATURES  ******************************
 ******************************************************************************/

/* event handler for checking freshness of host results */
static void check_host_result_freshness(struct nm_event_execution_properties *evprop)
{
	host *temp_host = NULL;
	time_t current_time = 0L;

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
		/* get the current time */
		time(&current_time);

		/* Reschedule, since recurring */
		schedule_event(host_freshness_check_interval, check_host_result_freshness, evprop->user_data);

		log_debug_info(DEBUGL_CHECKS, 2, "Attempting to check the freshness of host check results...\n");

		/* bail out if we're not supposed to be checking freshness */
		if (check_host_freshness == FALSE) {
			log_debug_info(DEBUGL_CHECKS, 2, "Host freshness checking is disabled.\n");
			return;
		}


		/* check all hosts... */
		for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

			/* skip hosts we shouldn't be checking for freshness */
			if (temp_host->check_freshness == FALSE)
				continue;

			/* skip hosts that have both active and passive checks disabled */
			if (temp_host->checks_enabled == FALSE && temp_host->accept_passive_checks == FALSE)
				continue;

			/* skip hosts that are currently executing (problems here will be caught by orphaned host check) */
			if (temp_host->is_executing == TRUE)
				continue;

			/* skip hosts that are already being freshened */
			if (temp_host->is_being_freshened == TRUE)
				continue;

			/* see if the time is right... */
			if (check_time_against_period(current_time, temp_host->check_period_ptr) == ERROR)
				continue;

			/* the results for the last check of this host are stale */
			if (is_host_result_fresh(temp_host, current_time, TRUE) == FALSE) {

				/* set the freshen flag */
				temp_host->is_being_freshened = TRUE;

				/* schedule an immediate forced check of the host */
				schedule_next_host_check(temp_host, 0, CHECK_OPTION_FORCE_EXECUTION | CHECK_OPTION_FRESHNESS_CHECK);
			}
		}
	}
}

/* check for hosts that never returned from a check... */
static void check_for_orphaned_hosts_eventhandler(struct nm_event_execution_properties *evprop)
{
	host *temp_host = NULL;
	time_t current_time = 0L;
	time_t expected_time = 0L;

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
		schedule_event(DEFAULT_ORPHAN_CHECK_INTERVAL, check_for_orphaned_hosts_eventhandler, evprop->user_data);

		/* get the current time */
		time(&current_time);

		/* check all hosts... */
		for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

			/* skip hosts that don't have a set check interval (on-demand checks are missed by the orphan logic) */
			if (temp_host->next_check == (time_t)0L)
				continue;

			/* skip hosts that are not currently executing */
			if (temp_host->is_executing == FALSE)
				continue;

			/* determine the time at which the check results should have come in (allow 10 minutes slack time) */
			expected_time = (time_t)(temp_host->next_check + temp_host->latency + host_check_timeout + check_reaper_interval + 600);

			/* this host was supposed to have executed a while ago, but for some reason the results haven't come back in... */
			if (expected_time < current_time) {

				/* log a warning */
				nm_log(NSLOG_RUNTIME_WARNING,
				       "Warning: The check of host '%s' looks like it was orphaned (results never came back).  I'm scheduling an immediate check of the host...\n", temp_host->name);

				log_debug_info(DEBUGL_CHECKS, 1, "Host '%s' was orphaned, so we're scheduling an immediate check...\n", temp_host->name);

				/* decrement the number of running host checks */
				if (currently_running_host_checks > 0)
					currently_running_host_checks--;

				/* disable the executing flag */
				temp_host->is_executing = FALSE;

				temp_host->last_update = current_time;

				/* schedule an immediate check of the host */
				schedule_next_host_check(temp_host, 0, CHECK_OPTION_ORPHAN_CHECK);
			}

		}
	}
}

/******************************************************************************
 ****************************  STATUS / IMMUTABLE  ****************************
 ******************************************************************************/

/* checks host dependencies */
int check_host_dependencies(host *hst, int dependency_type)
{
	hostdependency *temp_dependency = NULL;
	objectlist *list;
	host *temp_host = NULL;
	int state = STATE_UP;
	time_t current_time = 0L;

	log_debug_info(DEBUGL_CHECKS, 0, "Host '%s' check_host_dependencies()\n", hst->name);

	if (dependency_type == NOTIFICATION_DEPENDENCY) {
		list = hst->notify_deps;
	} else {
		list = hst->exec_deps;
	}

	/* check all dependencies... */
	for (; list; list = list->next) {
		temp_dependency = (hostdependency *)list->object_ptr;

		/* find the host we depend on... */
		if ((temp_host = temp_dependency->master_host_ptr) == NULL)
			continue;

		/* skip this dependency if it has a timeperiod and the current time isn't valid */
		time(&current_time);
		if (temp_dependency->dependency_period != NULL && check_time_against_period(current_time, temp_dependency->dependency_period_ptr) == ERROR)
			return FALSE;

		/* get the status to use (use last hard state if its currently in a soft state) */
		if (temp_host->state_type == SOFT_STATE && soft_state_dependencies == FALSE)
			state = temp_host->last_hard_state;
		else
			state = temp_host->current_state;

		log_debug_info(DEBUGL_CHECKS, 1, "  depending on host '%s' with state: %d / has_been_checked: %d\n", temp_host->name, state, temp_host->has_been_checked);

		/* is the host we depend on in state that fails the dependency tests? */
		if (flag_isset(temp_dependency->failure_options, 1 << state))
			return DEPENDENCIES_FAILED;

		/* check for pending flag */
		if (temp_host->has_been_checked == FALSE && flag_isset(temp_dependency->failure_options, OPT_PENDING))
			return DEPENDENCIES_FAILED;

		/* immediate dependencies ok at this point - check parent dependencies if necessary */
		if (temp_dependency->inherits_parent == TRUE) {
			if (check_host_dependencies(temp_host, dependency_type) != DEPENDENCIES_OK)
				return DEPENDENCIES_FAILED;
		}
	}

	return DEPENDENCIES_OK;
}

/* checks to see if a hosts's check results are fresh */
static int is_host_result_fresh(host *temp_host, time_t current_time, int log_this)
{
	time_t expiration_time = 0L;
	int freshness_threshold = 0;
	int days = 0;
	int hours = 0;
	int minutes = 0;
	int seconds = 0;
	int tdays = 0;
	int thours = 0;
	int tminutes = 0;
	int tseconds = 0;
	double interval = 0;

	log_debug_info(DEBUGL_CHECKS, 2, "Checking freshness of host '%s'...\n", temp_host->name);

	/* use user-supplied freshness threshold or auto-calculate a freshness threshold to use? */
	if (temp_host->freshness_threshold == 0) {
		if (temp_host->state_type == HARD_STATE || temp_host->current_state == STATE_OK) {
			interval = get_host_check_interval_s(temp_host);
		} else {
			interval = get_host_retry_interval_s(temp_host);
		}
		freshness_threshold = interval + temp_host->latency + additional_freshness_latency;
	} else
		freshness_threshold = temp_host->freshness_threshold;

	log_debug_info(DEBUGL_CHECKS, 2, "Freshness thresholds: host=%d, use=%d\n", temp_host->freshness_threshold, freshness_threshold);

	/* calculate expiration time */
	/*
	 * CHANGED 11/10/05 EG:
	 * program start is only used in expiration time calculation
	 * if > last check AND active checks are enabled, so active checks
	 * can become stale immediately upon program startup
	 */
	if (temp_host->has_been_checked == FALSE)
		expiration_time = (time_t)(event_start + freshness_threshold);
	/*
	 * CHANGED 06/19/07 EG:
	 * Per Ton's suggestion (and user requests), only use program start
	 * time over last check if no specific threshold has been set by user.
	 * Problems can occur if Nagios is restarted more frequently that
	 * freshness threshold intervals (hosts never go stale).
	 */
	else if (temp_host->checks_enabled == TRUE && event_start > temp_host->last_check && temp_host->freshness_threshold == 0)
		expiration_time = (time_t)(event_start + freshness_threshold);
	else
		expiration_time = (time_t)(temp_host->last_check + freshness_threshold);

	/*
	 * If the check was last done passively, we assume it's going
	 * to continue that way and we need to handle the fact that
	 * Nagios might have been shut off for quite a long time. If so,
	 * we mustn't spam freshness notifications but use event_start
	 * instead of last_check to determine freshness expiration time.
	 * The threshold for "long time" is determined as 61.8% of the normal
	 * freshness threshold based on vast heuristical research (ie, "some
	 * guy once told me the golden ratio is good for loads of stuff").
	 */
	if (temp_host->check_type == CHECK_TYPE_PASSIVE) {
		if (temp_host->last_check < event_start &&
		    event_start - last_program_stop > freshness_threshold * 0.618) {
			expiration_time = event_start + freshness_threshold;
		}
	}

	log_debug_info(DEBUGL_CHECKS, 2, "HBC: %d, PS: %lu, ES: %lu, LC: %lu, CT: %lu, ET: %lu\n", temp_host->has_been_checked, (unsigned long)program_start, (unsigned long)event_start, (unsigned long)temp_host->last_check, (unsigned long)current_time, (unsigned long)expiration_time);

	/* the results for the last check of this host are stale */
	if (expiration_time < current_time) {

		get_time_breakdown((current_time - expiration_time), &days, &hours, &minutes, &seconds);
		get_time_breakdown(freshness_threshold, &tdays, &thours, &tminutes, &tseconds);

		/* log a warning */
		if (log_this == TRUE)
			nm_log(NSLOG_RUNTIME_WARNING,
			       "Warning: The results of host '%s' are stale by %dd %dh %dm %ds (threshold=%dd %dh %dm %ds).  I'm forcing an immediate check of the host.\n", temp_host->name, days, hours, minutes, seconds, tdays, thours, tminutes, tseconds);

		log_debug_info(DEBUGL_CHECKS, 1, "Check results for host '%s' are stale by %dd %dh %dm %ds (threshold=%dd %dh %dm %ds).  Forcing an immediate check of the host...\n", temp_host->name, days, hours, minutes, seconds, tdays, thours, tminutes, tseconds);

		return FALSE;
	} else
		log_debug_info(DEBUGL_CHECKS, 1, "Check results for host '%s' are fresh.\n", temp_host->name);

	return TRUE;
}

static gboolean is_host_up(gpointer _name, gpointer _hst, gpointer user_data)
{
	host *hst = (host *)_hst;
	gboolean *retval = (gboolean *)user_data;
	if (hst->current_state == STATE_UP) {
		*retval = TRUE;
		return TRUE;
	}
	return FALSE;
}

/* determination of the host's state based on route availability*/
/* used only to determine difference between DOWN and UNREACHABLE states */
static int determine_host_reachability(host *hst)
{
	gboolean *is_up_result;
	log_debug_info(DEBUGL_CHECKS, 2, "Determining state of host '%s': current state=%d (%s)\n", hst->name, hst->current_state, host_state_name(hst->current_state));

	/* host is UP - no translation needed */
	if (hst->current_state == STATE_UP) {
		log_debug_info(DEBUGL_CHECKS, 2, "Host is UP, no state translation needed.\n");
		return STATE_UP;
	}

	if (g_tree_nnodes(hst->parent_hosts) == 0)
		return STATE_DOWN;

	is_up_result = FALSE;
	g_tree_foreach(hst->parent_hosts, is_host_up, &is_up_result);
	if (is_up_result)
		return STATE_DOWN;

	log_debug_info(DEBUGL_CHECKS, 2, "No parents were up, so host is UNREACHABLE.\n");
	return STATE_UNREACHABLE;
}
