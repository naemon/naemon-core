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

#ifdef USE_EVENT_BROKER
#include "neberrors.h"
#endif


static void check_service_result_freshness(void *arg);
static void handle_service_check_event(void *arg);
static void handle_worker_service_check(wproc_result *wpres, void *arg, int flags);

static int is_service_result_fresh(service *, time_t, int);
static int check_service_check_viability(service *, int, int *, time_t *);
static int run_scheduled_service_check(service *, int, double);
static int run_async_service_check(service *, int, double, int, int, int *, time_t *);


void checks_init_services(void)
{
	service *temp_service = NULL;
	time_t current_time = time(NULL);

	/******** GET BASIC SERVICE INFO  ********/

	/* get info on service checks to be scheduled */
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		/* maybe we shouldn't schedule this check */
		if (temp_service->check_interval == 0 || !temp_service->checks_enabled) {
			log_debug_info(DEBUGL_EVENTS, 1, "Service '%s' on host '%s' should not be scheduled.\n", temp_service->description, temp_service->host_name);
			temp_service->should_be_scheduled = FALSE;
			continue;
		}
	}

	/******** SCHEDULE SERVICE CHECKS  ********/

	log_debug_info(DEBUGL_EVENTS, 2, "Scheduling service checks...");

	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {
		log_debug_info(DEBUGL_EVENTS, 2, "Service '%s' on host '%s'\n", temp_service->description, temp_service->host_name);
		/* skip this service if it shouldn't be scheduled */
		if (temp_service->should_be_scheduled == FALSE) {
			continue;
		}

		temp_service->next_check = current_time + ranged_urand(0, check_window(temp_service));

		log_debug_info(DEBUGL_EVENTS, 2, "Check Time: %lu --> %s", (unsigned long)temp_service->next_check, ctime(&temp_service->next_check));
	}

	/* add scheduled service checks to event queue */
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		/* update status of all services (scheduled or not) */
		update_service_status(temp_service, FALSE);

		/* skip most services that shouldn't be scheduled */
		if (temp_service->should_be_scheduled == FALSE) {

			/* passive checks are an exception if a forced check was scheduled before we restarted */
			if (!(temp_service->checks_enabled == FALSE && temp_service->next_check != (time_t)0L && (temp_service->check_options & CHECK_OPTION_FORCE_EXECUTION)))
				continue;
		}

		/* create a new service check event */
		temp_service->next_check_event = schedule_event(temp_service->next_check - time(NULL), handle_service_check_event, (void *)temp_service);
	}

	/* add a service result "freshness" check event */
	if (check_service_freshness == TRUE) {
		schedule_event(service_freshness_check_interval, check_service_result_freshness, NULL);
	}
}


static void handle_service_check_event(void *arg)
{
	service *temp_service = (service *)arg;
	int run_event = TRUE;	/* default action is to execute the event */
	int nudge_seconds = 0;
	double latency;
	struct timeval tv;
	struct timeval event_runtime;

	/* get event latency */
	gettimeofday(&tv, NULL);
	event_runtime.tv_sec = temp_service->next_check;
	event_runtime.tv_usec = 0;
	latency = (double)(tv_delta_f(&event_runtime, &tv));

	/* forced checks override normal check logic */
	if (!(temp_service->check_options & CHECK_OPTION_FORCE_EXECUTION)) {
		/* don't run a service check if we're already maxed out on the number of parallel service checks...  */
		if (max_parallel_service_checks != 0 && (currently_running_service_checks >= max_parallel_service_checks)) {
			nudge_seconds = ranged_urand(5, 17);
			nm_log(NSLOG_RUNTIME_WARNING,
			       "\tMax concurrent service checks (%d) has been reached.  Nudging %s:%s by %d seconds...\n", max_parallel_service_checks, temp_service->host_name, temp_service->description, nudge_seconds);
			run_event = FALSE;
		}

		/* don't run a service check if active checks are disabled */
		if (execute_service_checks == FALSE) {
			log_debug_info(DEBUGL_EVENTS | DEBUGL_CHECKS, 1, "We're not executing service checks right now, so we'll skip check event for service '%s;%s'.\n", temp_service->host_name, temp_service->description);
			run_event = FALSE;
		}

	}

	/* reschedule the check if we can't run it now */
	if (run_event == FALSE) {
		if (nudge_seconds) {
			/* We nudge the next check time when it is due to too many concurrent service checks */
			temp_service->next_check = (time_t)(temp_service->next_check + nudge_seconds);
		} else {
			temp_service->next_check += check_window(temp_service);
		}
		temp_service->next_check_event = schedule_event(temp_service->next_check - time(NULL), handle_service_check_event, (void*)temp_service);
	} else {
		/* Otherwise, run the event */
		run_scheduled_service_check(temp_service, temp_service->check_options, latency);
	}
}

static void handle_worker_service_check(wproc_result *wpres, void *arg, int flags)
{
	check_result *cr = (check_result *)arg;
	if(wpres) {
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
	free_check_result(cr);
	free(cr);
}


/* executes a scheduled service check */
static int run_scheduled_service_check(service *svc, int check_options, double latency)
{
	int result = OK;
	time_t preferred_time = 0L;
	int time_is_valid = TRUE;

	if (svc == NULL)
		return ERROR;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "run_scheduled_service_check() start\n");
	log_debug_info(DEBUGL_CHECKS, 0, "Attempting to run scheduled check of service '%s' on host '%s': check options=%d, latency=%lf\n", svc->description, svc->host_name, check_options, latency);

	/*
	 * reset the next_check_event so we know it's
	 * no longer in the scheduling queue
	 */
	svc->next_check_event = NULL;

	/* attempt to run the check */
	result = run_async_service_check(svc, check_options, latency, TRUE, TRUE, &time_is_valid, &preferred_time);

	if (result == OK)
		return OK;

	/* an error occurred, so reschedule the check */
	log_debug_info(DEBUGL_CHECKS, 1, "Unable to run scheduled service check at this time\n");

	/* only attempt to (re)schedule checks that should get checked... */
	if (svc->should_be_scheduled == FALSE)
		return ERROR;

	/* reschedule */
	svc->next_check = next_check_time(svc);
	schedule_service_check(svc, svc->next_check, check_options);

	/* update the status log */
	update_service_status(svc, FALSE);

	return ERROR;
}


/* forks a child process to run a service check, but does not wait for the service check result */
static int run_async_service_check(service *svc, int check_options, double latency, int scheduled_check, int reschedule_check, int *time_is_valid, time_t *preferred_time)
{
	nagios_macros mac;
	char *raw_command = NULL;
	char *processed_command = NULL;
	struct timeval start_time, end_time;
	host *temp_host = NULL;
	double old_latency = 0.0;
	check_result *cr;
	int runchk_result = OK;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
#ifdef USE_EVENT_BROKER
	int neb_result = OK;
#endif

	log_debug_info(DEBUGL_FUNCTIONS, 0, "run_async_service_check()\n");

	/* make sure we have something */
	if (svc == NULL)
		return ERROR;

	/* is the service check viable at this time? */
	if (check_service_check_viability(svc, check_options, time_is_valid, preferred_time) == ERROR)
		return ERROR;

	temp_host = svc->host_ptr;

	/******** GOOD TO GO FOR A REAL SERVICE CHECK AT THIS POINT ********/

#ifdef USE_EVENT_BROKER
	/* initialize start/end times */
	start_time.tv_sec = 0L;
	start_time.tv_usec = 0L;
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;

	/* send data to event broker */
	neb_result = broker_service_check(NEBTYPE_SERVICECHECK_ASYNC_PRECHECK, NEBFLAG_NONE, NEBATTR_NONE, svc, CHECK_TYPE_ACTIVE, start_time, end_time, svc->check_command, svc->latency, 0.0, 0, FALSE, 0, NULL, NULL, NULL);

	if (neb_result == NEBERROR_CALLBACKCANCEL || neb_result == NEBERROR_CALLBACKOVERRIDE) {
		log_debug_info(DEBUGL_CHECKS, 0, "Check of service '%s' on host '%s' (id=%u) was %s by a module\n",
		               svc->description, svc->host_name, svc->id,
		               neb_result == NEBERROR_CALLBACKCANCEL ? "cancelled" : "overridden");
	}
	/* neb module wants to cancel the service check - the check will be rescheduled for a later time by the scheduling logic */
	if (neb_result == NEBERROR_CALLBACKCANCEL) {
		if (preferred_time)
			*preferred_time += (svc->check_interval * interval_length);
		return ERROR;
	}

	/* neb module wants to override (or cancel) the service check - perhaps it will check the service itself */
	/* NOTE: if a module does this, it has to do a lot of the stuff found below to make sure things don't get whacked out of shape! */
	/* NOTE: if would be easier for modules to override checks when the NEBTYPE_SERVICECHECK_INITIATE event is called (later) */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE)
		return OK;
#endif


	log_debug_info(DEBUGL_CHECKS, 0, "Checking service '%s' on host '%s'...\n", svc->description, svc->host_name);

	/* clear check options - we don't want old check options retained */
	/* only clear check options for scheduled checks - ondemand checks shouldn't affected retained check options */
	if (scheduled_check == TRUE)
		svc->check_options = CHECK_OPTION_NONE;

	/* update latency for macros, event broker, save old value for later */
	old_latency = svc->latency;
	svc->latency = latency;

	/* grab the host and service macro variables */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, temp_host);
	grab_service_macros_r(&mac, svc);

	/* get the raw command line */
	get_raw_command_line_r(&mac, svc->check_command_ptr, svc->check_command, &raw_command, macro_options);
	if (raw_command == NULL) {
		clear_volatile_macros_r(&mac);
		log_debug_info(DEBUGL_CHECKS, 0, "Raw check command for service '%s' on host '%s' was NULL - aborting.\n", svc->description, svc->host_name);
		if (preferred_time)
			*preferred_time += (svc->check_interval * interval_length);
		svc->latency = old_latency;
		return ERROR;
	}

	/* process any macros contained in the argument */
	process_macros_r(&mac, raw_command, &processed_command, macro_options);
	my_free(raw_command);
	if (processed_command == NULL) {
		clear_volatile_macros_r(&mac);
		log_debug_info(DEBUGL_CHECKS, 0, "Processed check command for service '%s' on host '%s' was NULL - aborting.\n", svc->description, svc->host_name);
		if (preferred_time)
			*preferred_time += (svc->check_interval * interval_length);
		svc->latency = old_latency;
		return ERROR;
	}

	/* get the command start time */
	gettimeofday(&start_time, NULL);

	cr = nm_calloc(1, sizeof(*cr));
	init_check_result(cr);

	/* save check info */
	cr->object_check_type = SERVICE_CHECK;
	cr->check_type = CHECK_TYPE_ACTIVE;
	cr->check_options = check_options;
	cr->scheduled_check = scheduled_check;
	cr->reschedule_check = reschedule_check;
	cr->latency = latency;
	cr->start_time = start_time;
	cr->finish_time = start_time;
	cr->early_timeout = FALSE;
	cr->exited_ok = TRUE;
	cr->return_code = STATE_OK;
	cr->output = NULL;
	cr->host_name = nm_strdup(svc->host_name);
	cr->service_description = nm_strdup(svc->description);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	neb_result = broker_service_check(NEBTYPE_SERVICECHECK_INITIATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CHECK_TYPE_ACTIVE, start_time, end_time, svc->check_command, svc->latency, 0.0, service_check_timeout, FALSE, 0, processed_command, NULL, cr);

	/* neb module wants to override the service check - perhaps it will check the service itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE) {
		clear_volatile_macros_r(&mac);
		svc->latency = old_latency;
		free_check_result(cr);
		my_free(processed_command);
		return OK;
	}
#endif

	/* reset latency (permanent value will be set later) */
	svc->latency = old_latency;

	/* paw off the check to a worker to run */
	runchk_result = wproc_run_callback(processed_command, service_check_timeout, handle_worker_service_check, (void*)cr, &mac);
	if (runchk_result == ERROR) {
		nm_log(NSLOG_RUNTIME_ERROR,
		       "Unable to run check for service '%s' on host '%s'\n", svc->description, svc->host_name);
	} else {
		/* do the book-keeping */
		currently_running_service_checks++;
		svc->is_executing = TRUE;
		update_check_stats((scheduled_check == TRUE) ? ACTIVE_SCHEDULED_SERVICE_CHECK_STATS : ACTIVE_ONDEMAND_SERVICE_CHECK_STATS, start_time.tv_sec);
	}

	/* free memory */
	my_free(processed_command);
	clear_volatile_macros_r(&mac);

	return OK;
}


/* handles asynchronous service check results */
int handle_async_service_check_result(service *temp_service, check_result *queued_check_result)
{
	host *temp_host = NULL;
	time_t next_service_check = 0L;
	int reschedule_check = FALSE;
	int state_change = FALSE;
	int hard_state_change = FALSE;
	int first_host_check_initiated = FALSE;
	int route_result = HOST_UP;
	time_t current_time = 0L;
	int alert_recorded = NEBATTR_NONE;
	int first_recorded_state = NEBATTR_NONE;
	char *old_plugin_output = NULL;
	char *old_long_plugin_output = NULL;
	char *temp_plugin_output = NULL;
	char *temp_ptr = NULL;
	servicedependency *temp_dependency = NULL;
	service *master_service = NULL;
	int state_changes_use_cached_state = TRUE; /* TODO - 09/23/07 move this to a global variable */
	int flapping_check_done = FALSE;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "handle_async_service_check_result()\n");

	/* make sure we have what we need */
	if (temp_service == NULL || queued_check_result == NULL)
		return ERROR;

	/* get the current time */
	time(&current_time);

	next_service_check = current_time + check_window(temp_service);

	log_debug_info(DEBUGL_CHECKS, 0, "** Handling check result for service '%s' on host '%s' from '%s'...\n", temp_service->description, temp_service->host_name, check_result_source(queued_check_result));
	log_debug_info(DEBUGL_CHECKS, 1, "HOST: %s, SERVICE: %s, CHECK TYPE: %s, OPTIONS: %d, SCHEDULED: %s, RESCHEDULE: %s, EXITED OK: %s, RETURN CODE: %d, OUTPUT: %s\n", temp_service->host_name, temp_service->description, (queued_check_result->check_type == CHECK_TYPE_ACTIVE) ? "Active" : "Passive", queued_check_result->check_options, (queued_check_result->scheduled_check == TRUE) ? "Yes" : "No", (queued_check_result->reschedule_check == TRUE) ? "Yes" : "No", (queued_check_result->exited_ok == TRUE) ? "Yes" : "No", queued_check_result->return_code, queued_check_result->output);

	/* decrement the number of service checks still out there... */
	if (queued_check_result->check_type == CHECK_TYPE_ACTIVE && currently_running_service_checks > 0)
		currently_running_service_checks--;

	/* skip this service check results if its passive and we aren't accepting passive check results */
	if (queued_check_result->check_type == CHECK_TYPE_PASSIVE) {
		if (accept_passive_service_checks == FALSE) {
			log_debug_info(DEBUGL_CHECKS, 0, "Discarding passive service check result because passive service checks are disabled globally.\n");
			return ERROR;
		}
		if (temp_service->accept_passive_checks == FALSE) {
			log_debug_info(DEBUGL_CHECKS, 0, "Discarding passive service check result because passive checks are disabled for this service.\n");
			return ERROR;
		}
	}

	/* clear the freshening flag (it would have been set if this service was determined to be stale) */
	if (queued_check_result->check_options & CHECK_OPTION_FRESHNESS_CHECK)
		temp_service->is_being_freshened = FALSE;

	/* clear the execution flag if this was an active check */
	if (queued_check_result->check_type == CHECK_TYPE_ACTIVE)
		temp_service->is_executing = FALSE;

	/* DISCARD INVALID FRESHNESS CHECK RESULTS */
	/* If a services goes stale, Nagios will initiate a forced check in order to freshen it.  There is a race condition whereby a passive check
	   could arrive between the 1) initiation of the forced check and 2) the time when the forced check result is processed here.  This would
	   make the service fresh again, so we do a quick check to make sure the service is still stale before we accept the check result. */
	if ((queued_check_result->check_options & CHECK_OPTION_FRESHNESS_CHECK) && is_service_result_fresh(temp_service, current_time, FALSE) == TRUE) {
		log_debug_info(DEBUGL_CHECKS, 0, "Discarding service freshness check result because the service is currently fresh (race condition avoided).\n");
		return OK;
	}

	/* check latency is passed to us */
	temp_service->latency = queued_check_result->latency;

	/* update the execution time for this check (millisecond resolution) */
	temp_service->execution_time = (double)((double)(queued_check_result->finish_time.tv_sec - queued_check_result->start_time.tv_sec) + (double)((queued_check_result->finish_time.tv_usec - queued_check_result->start_time.tv_usec) / 1000.0) / 1000.0);
	if (temp_service->execution_time < 0.0)
		temp_service->execution_time = 0.0;

	/* get the last check time */
	if (!temp_service->last_check)
		first_recorded_state = NEBATTR_CHECK_FIRST;
	temp_service->last_check = queued_check_result->start_time.tv_sec;

	/* was this check passive or active? */
	temp_service->check_type = (queued_check_result->check_type == CHECK_TYPE_ACTIVE) ? CHECK_TYPE_ACTIVE : CHECK_TYPE_PASSIVE;

	/* update check statistics for passive checks */
	if (queued_check_result->check_type == CHECK_TYPE_PASSIVE)
		update_check_stats(PASSIVE_SERVICE_CHECK_STATS, queued_check_result->start_time.tv_sec);

	/* should we reschedule the next service check? NOTE: This may be overridden later... */
	reschedule_check = queued_check_result->reschedule_check;

	/* save the old service status info */
	temp_service->last_state = temp_service->current_state;

	/* save old plugin output */
	if (temp_service->plugin_output)
		old_plugin_output = nm_strdup(temp_service->plugin_output);

	if (temp_service->long_plugin_output)
		old_long_plugin_output = strdup(temp_service->long_plugin_output);
	/* clear the old plugin output and perf data buffers */
	my_free(temp_service->plugin_output);
	my_free(temp_service->long_plugin_output);
	my_free(temp_service->perf_data);

	if (queued_check_result->early_timeout == TRUE) {
		nm_log(NSLOG_RUNTIME_WARNING,
		       "Warning: Check of service '%s' on host '%s' timed out after %.3fs!\n", temp_service->description, temp_service->host_name, temp_service->execution_time);
		nm_asprintf(&temp_service->plugin_output, "(Service check timed out after %.2lf seconds)\n", temp_service->execution_time);
		temp_service->current_state = service_check_timeout_state;
	}
	/* if there was some error running the command, just skip it (this shouldn't be happening) */
	else if (queued_check_result->exited_ok == FALSE) {

		nm_log(NSLOG_RUNTIME_WARNING,
		       "Warning:  Check of service '%s' on host '%s' did not exit properly!\n", temp_service->description, temp_service->host_name);

		temp_service->plugin_output = nm_strdup("(Service check did not exit properly)");

		temp_service->current_state = STATE_CRITICAL;
	}

	/* make sure the return code is within bounds */
	else if (queued_check_result->return_code < 0 || queued_check_result->return_code > 3) {

		nm_log(NSLOG_RUNTIME_WARNING,
		       "Warning: Return code of %d for check of service '%s' on host '%s' was out of bounds.%s\n", queued_check_result->return_code, temp_service->description, temp_service->host_name, (queued_check_result->return_code == 126 ? "Make sure the plugin you're trying to run is executable." : (queued_check_result->return_code == 127 ? " Make sure the plugin you're trying to run actually exists." : "")));

		nm_asprintf(&temp_plugin_output, "\x73\x6f\x69\x67\x61\x6e\x20\x74\x68\x67\x69\x72\x79\x70\x6f\x63\x20\x6e\x61\x68\x74\x65\x20\x64\x61\x74\x73\x6c\x61\x67");
		my_free(temp_plugin_output);
		nm_asprintf(&temp_service->plugin_output, "(Return code of %d is out of bounds%s)", queued_check_result->return_code, (queued_check_result->return_code == 126 ? " - plugin may not be executable" : (queued_check_result->return_code == 127 ? " - plugin may be missing" : "")));

		temp_service->current_state = STATE_CRITICAL;
	}

	/* else the return code is okay... */
	else {

		/* parse check output to get: (1) short output, (2) long output, (3) perf data */
		parse_check_output(queued_check_result->output, &temp_service->plugin_output, &temp_service->long_plugin_output, &temp_service->perf_data, TRUE, FALSE);

		/* make sure the plugin output isn't null */
		if (temp_service->plugin_output == NULL)
			temp_service->plugin_output = nm_strdup("(No output returned from plugin)");

		/* replace semicolons in plugin output (but not performance data) with colons */
		else if ((temp_ptr = temp_service->plugin_output)) {
			while ((temp_ptr = strchr(temp_ptr, ';')))
				* temp_ptr = ':';
		}

		log_debug_info(DEBUGL_CHECKS, 2, "Parsing check output...\n");
		log_debug_info(DEBUGL_CHECKS, 2, "Short Output: %s\n", (temp_service->plugin_output == NULL) ? "NULL" : temp_service->plugin_output);
		log_debug_info(DEBUGL_CHECKS, 2, "Long Output:  %s\n", (temp_service->long_plugin_output == NULL) ? "NULL" : temp_service->long_plugin_output);
		log_debug_info(DEBUGL_CHECKS, 2, "Perf Data:    %s\n", (temp_service->perf_data == NULL) ? "NULL" : temp_service->perf_data);

		/* grab the return code */
		temp_service->current_state = queued_check_result->return_code;
	}


	/* record the time the last state ended */
	switch (temp_service->last_state) {
	case STATE_OK:
		temp_service->last_time_ok = temp_service->last_check;
		break;
	case STATE_WARNING:
		temp_service->last_time_warning = temp_service->last_check;
		break;
	case STATE_UNKNOWN:
		temp_service->last_time_unknown = temp_service->last_check;
		break;
	case STATE_CRITICAL:
		temp_service->last_time_critical = temp_service->last_check;
		break;
	default:
		break;
	}

	/* log passive checks - we need to do this here, as some my bypass external commands by getting dropped in checkresults dir */
	if (temp_service->check_type == CHECK_TYPE_PASSIVE) {
		if (log_passive_checks == TRUE)
			nm_log(NSLOG_PASSIVE_CHECK,
			       "PASSIVE SERVICE CHECK: %s;%s;%d;%s\n", temp_service->host_name, temp_service->description, temp_service->current_state, temp_service->plugin_output);
	}

	temp_host = temp_service->host_ptr;

	/* if the service check was okay... */
	if (temp_service->current_state == STATE_OK) {

		/* if the host has never been checked before, verify its status */
		/* only do this if 1) the initial state was set to non-UP or 2) the host is not scheduled to be checked soon (next 5 minutes) */
		if (temp_host->has_been_checked == FALSE && (temp_host->initial_state != HOST_UP || (unsigned long)temp_host->next_check == 0L || (unsigned long)(temp_host->next_check - current_time) > 300)) {

			/* set a flag to remember that we launched a check */
			first_host_check_initiated = TRUE;
			schedule_host_check(temp_host, current_time, CHECK_OPTION_DEPENDENCY_CHECK);
		}
	}


	/* increment the current attempt number if this is a soft state (service was rechecked) */
	if (temp_service->state_type == SOFT_STATE && (temp_service->current_attempt < temp_service->max_attempts))
		temp_service->current_attempt = temp_service->current_attempt + 1;


	log_debug_info(DEBUGL_CHECKS, 2, "ST: %s  CA: %d  MA: %d  CS: %d  LS: %d  LHS: %d\n", (temp_service->state_type == SOFT_STATE) ? "SOFT" : "HARD", temp_service->current_attempt, temp_service->max_attempts, temp_service->current_state, temp_service->last_state, temp_service->last_hard_state);

	/* check for a state change (either soft or hard) */
	if (temp_service->current_state != temp_service->last_state) {
		log_debug_info(DEBUGL_CHECKS, 2, "Service has changed state since last check!\n");
		state_change = TRUE;
	}

	/* checks for a hard state change where host was down at last service check */
	/* this occurs in the case where host goes down and service current attempt gets reset to 1 */
	/* if this check is not made, the service recovery looks like a soft recovery instead of a hard one */
	if (temp_service->host_problem_at_last_check == TRUE && temp_service->current_state == STATE_OK) {
		log_debug_info(DEBUGL_CHECKS, 2, "Service had a HARD STATE CHANGE!!\n");
		hard_state_change = TRUE;
	}

	/* check for a "normal" hard state change where max check attempts is reached */
	if (temp_service->current_attempt >= temp_service->max_attempts && temp_service->current_state != temp_service->last_hard_state) {
		log_debug_info(DEBUGL_CHECKS, 2, "Service had a HARD STATE CHANGE!!\n");
		hard_state_change = TRUE;
	}

	/* a state change occurred... */
	/* reset last and next notification times and acknowledgement flag if necessary, misc other stuff */
	if (state_change == TRUE || hard_state_change == TRUE) {

		/* reschedule the service check */
		reschedule_check = TRUE;

		/* reset notification times */
		temp_service->last_notification = (time_t)0;
		temp_service->next_notification = (time_t)0;

		/* reset notification suppression option */
		temp_service->no_more_notifications = FALSE;

		if (temp_service->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL && (state_change == TRUE || hard_state_change == FALSE)) {

			temp_service->problem_has_been_acknowledged = FALSE;
			temp_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;

			/* remove any non-persistant comments associated with the ack */
			delete_service_acknowledgement_comments(temp_service);
		} else if (temp_service->acknowledgement_type == ACKNOWLEDGEMENT_STICKY && temp_service->current_state == STATE_OK) {

			temp_service->problem_has_been_acknowledged = FALSE;
			temp_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;

			/* remove any non-persistant comments associated with the ack */
			delete_service_acknowledgement_comments(temp_service);
		}

		/*
		 * hard changes between non-OK states should continue
		 * to be escalated, so don't reset current notification number
		 */
	}

	/* initialize the last host and service state change times if necessary */
	if (temp_service->last_state_change == (time_t)0)
		temp_service->last_state_change = temp_service->last_check;
	if (temp_service->last_hard_state_change == (time_t)0)
		temp_service->last_hard_state_change = temp_service->last_check;
	if (temp_host->last_state_change == (time_t)0)
		temp_host->last_state_change = temp_service->last_check;
	if (temp_host->last_hard_state_change == (time_t)0)
		temp_host->last_hard_state_change = temp_service->last_check;

	/* update last service state change times */
	if (state_change == TRUE)
		temp_service->last_state_change = temp_service->last_check;
	if (hard_state_change == TRUE)
		temp_service->last_hard_state_change = temp_service->last_check;

	/* update the event and problem ids */
	if (state_change == TRUE) {

		/* always update the event id on a state change */
		temp_service->last_event_id = temp_service->current_event_id;
		temp_service->current_event_id = next_event_id;
		next_event_id++;

		/* update the problem id when transitioning to a problem state */
		if (temp_service->last_state == STATE_OK) {
			/* don't reset last problem id, or it will be zero the next time a problem is encountered */
			temp_service->current_problem_id = next_problem_id;
			next_problem_id++;
		}

		/* clear the problem id when transitioning from a problem state to an OK state */
		if (temp_service->current_state == STATE_OK) {
			temp_service->last_problem_id = temp_service->current_problem_id;
			temp_service->current_problem_id = 0L;
		}
	}


	/**************************************/
	/******* SERVICE CHECK OK LOGIC *******/
	/**************************************/

	/* if the service is up and running OK... */
	if (temp_service->current_state == STATE_OK) {

		log_debug_info(DEBUGL_CHECKS, 1, "Service is OK.\n");

		/* reset the acknowledgement flag (this should already have been done, but just in case...) */
		temp_service->problem_has_been_acknowledged = FALSE;
		temp_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;

		/* verify the route to the host and send out host recovery notifications */
		if (temp_host->current_state != HOST_UP) {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is NOT UP, so we'll check it to see if it recovered...\n");

			if (first_host_check_initiated == TRUE)
				log_debug_info(DEBUGL_CHECKS, 1, "First host check was already initiated, so we'll skip a new host check.\n");
			else {
				/* can we use the last cached host state? */
				/* usually only use cached host state if no service state change has occurred */
				if ((state_change == FALSE || state_changes_use_cached_state == TRUE) && temp_host->has_been_checked == TRUE && ((current_time - temp_host->last_check) <= cached_host_check_horizon)) {
					log_debug_info(DEBUGL_CHECKS, 1, "* Using cached host state: %d\n", temp_host->current_state);
					update_check_stats(ACTIVE_ONDEMAND_HOST_CHECK_STATS, current_time);
					update_check_stats(ACTIVE_CACHED_HOST_CHECK_STATS, current_time);
				}

				/* else launch an async (parallel) check of the host */
				else
					schedule_host_check(temp_host, current_time, CHECK_OPTION_DEPENDENCY_CHECK);
			}
		}

		/* if a hard service recovery has occurred... */
		if (hard_state_change == TRUE) {

			log_debug_info(DEBUGL_CHECKS, 1, "Service experienced a HARD RECOVERY.\n");

			/* set the state type macro */
			temp_service->state_type = HARD_STATE;

			/* log the service recovery */
			log_service_event(temp_service);
			alert_recorded = NEBATTR_CHECK_ALERT;

			/* 10/04/07 check to see if the service and/or associate host is flapping */
			/* this should be done before a notification is sent out to ensure the host didn't just start flapping */
			check_for_service_flapping(temp_service, TRUE, TRUE);
			check_for_host_flapping(temp_host, TRUE, FALSE, TRUE);
			flapping_check_done = TRUE;

			/* notify contacts about the service recovery */
			service_notification(temp_service, NOTIFICATION_NORMAL, NULL, NULL, NOTIFICATION_OPTION_NONE);

			/* run the service event handler to handle the hard state change */
			handle_service_event(temp_service);
		}

		/* else if a soft service recovery has occurred... */
		else if (state_change == TRUE) {

			log_debug_info(DEBUGL_CHECKS, 1, "Service experienced a SOFT RECOVERY.\n");

			/* this is a soft recovery */
			temp_service->state_type = SOFT_STATE;

			/* log the soft recovery */
			log_service_event(temp_service);
			alert_recorded = NEBATTR_CHECK_ALERT;

			/* run the service event handler to handle the soft state change */
			handle_service_event(temp_service);
		}

		/* else no service state change has occurred... */
		else {
			log_debug_info(DEBUGL_CHECKS, 1, "Service did not change state.\n");
		}

		/* should we obsessive over service checks? */
		if (obsess_over_services == TRUE)
			obsessive_compulsive_service_check_processor(temp_service);

		/* reset all service variables because its okay now... */
		temp_service->host_problem_at_last_check = FALSE;
		temp_service->current_attempt = 1;
		temp_service->state_type = HARD_STATE;
		temp_service->last_hard_state = STATE_OK;
		temp_service->last_notification = (time_t)0;
		temp_service->next_notification = (time_t)0;
		temp_service->current_notification_number = 0;
		temp_service->problem_has_been_acknowledged = FALSE;
		temp_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
		temp_service->notified_on = 0;

		if (reschedule_check == TRUE)
			next_service_check = (time_t)(temp_service->last_check + (temp_service->check_interval * interval_length));
	}


	/*******************************************/
	/******* SERVICE CHECK PROBLEM LOGIC *******/
	/*******************************************/

	/* hey, something's not working quite like it should... */
	else {

		log_debug_info(DEBUGL_CHECKS, 1, "Service is in a non-OK state!\n");
		/*
		 * the route-result is always the current host state, since we
		 * run checks asynchronously. The optimal solution would be to
		 * queue up services for notification once the host/route check
		 * is completed, but assuming max_attempts > 1, this will work
		 * just as well in practice
		 */
		route_result = temp_host->current_state;

		/* check the route to the host if its up right now... */
		if (temp_host->current_state == HOST_UP) {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is currently UP, so we'll recheck its state to make sure...\n");

			/* only run a new check if we can and have to */
			if (execute_host_checks && temp_host->last_check + cached_host_check_horizon < current_time) {
				schedule_host_check(temp_host, current_time, CHECK_OPTION_DEPENDENCY_CHECK);
			} else {
				log_debug_info(DEBUGL_CHECKS, 1, "* Using cached host state: %d\n", temp_host->current_state);
				update_check_stats(ACTIVE_ONDEMAND_HOST_CHECK_STATS, current_time);
				update_check_stats(ACTIVE_CACHED_HOST_CHECK_STATS, current_time);
			}
		}

		/* else the host is either down or unreachable, so recheck it if necessary */
		else {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is currently %s.\n", host_state_name(temp_host->current_state));

			if (execute_host_checks) {
				schedule_host_check(temp_host, current_time, CHECK_OPTION_NONE);
			}
			/* else fake the host check, but (possibly) resend host notifications to contacts... */
			else {

				log_debug_info(DEBUGL_CHECKS, 1, "Assuming host is in same state as before...\n");

				/* if the host has never been checked before, set the checked flag and last check time */
				/* This probably never evaluates to FALSE, present for historical reasons only, can probably be removed in the future */
				if (temp_host->has_been_checked == FALSE) {
					temp_host->has_been_checked = TRUE;
					temp_host->last_check = temp_service->last_check;
				}

				/* fake the route check result */
				route_result = temp_host->current_state;

				/* possibly re-send host notifications... */
				host_notification(temp_host, NOTIFICATION_NORMAL, NULL, NULL, NOTIFICATION_OPTION_NONE);
			}
		}

		/* if the host is down or unreachable ... */
		/* The host might be in a SOFT problem state due to host check retries/caching.  Not sure if we should take that into account and do something different or not... */
		if (route_result != HOST_UP) {

			log_debug_info(DEBUGL_CHECKS, 2, "Host is not UP, so we mark state changes if appropriate\n");

			/* "fake" a hard state change for the service - well, its not really fake, but it didn't get caught earlier... */
			if (temp_service->last_hard_state != temp_service->current_state)
				hard_state_change = TRUE;

			/* update last state change times */
			if (state_change == TRUE || hard_state_change == TRUE)
				temp_service->last_state_change = temp_service->last_check;
			if (hard_state_change == TRUE) {
				temp_service->last_hard_state_change = temp_service->last_check;
				temp_service->state_type = HARD_STATE;
				temp_service->last_hard_state = temp_service->current_state;
			}

			/* put service into a hard state without attempting check retries and don't send out notifications about it */
			temp_service->host_problem_at_last_check = TRUE;
		}

		/* the host is up - it recovered since the last time the service was checked... */
		else if (temp_service->host_problem_at_last_check == TRUE) {

			/* next time the service is checked we shouldn't get into this same case... */
			temp_service->host_problem_at_last_check = FALSE;

			/* reset the current check counter, so we give the service a chance */
			/* this helps prevent the case where service has N max check attempts, N-1 of which have already occurred. */
			/* if we didn't do this, the next check might fail and result in a hard problem - we should really give it more time */
			/* ADDED IF STATEMENT 01-17-05 EG */
			/* 01-17-05: Services in hard problem states before hosts went down would sometimes come back as soft problem states after */
			/* the hosts recovered.  This caused problems, so hopefully this will fix it */
			if (temp_service->state_type == SOFT_STATE)
				temp_service->current_attempt = 1;
		}

		log_debug_info(DEBUGL_CHECKS, 1, "Current/Max Attempt(s): %d/%d\n", temp_service->current_attempt, temp_service->max_attempts);

		/* if we should retry the service check, do so (except if the host is down or unreachable!) */
		if (temp_service->current_attempt < temp_service->max_attempts) {

			/* the host is down or unreachable, so don't attempt to retry the service check */
			if (route_result != HOST_UP) {

				log_debug_info(DEBUGL_CHECKS, 1, "Host isn't UP, so we won't retry the service check...\n");

				/* the host is not up, so reschedule the next service check at regular interval */
				if (reschedule_check == TRUE)
					next_service_check = (time_t)(temp_service->last_check + (temp_service->check_interval * interval_length));

				/* log the problem as a hard state if the host just went down */
				if (hard_state_change == TRUE) {
					log_service_event(temp_service);
					alert_recorded = NEBATTR_CHECK_ALERT;

					/* run the service event handler to handle the hard state */
					handle_service_event(temp_service);
				}
			}

			/* the host is up, so continue to retry the service check */
			else {

				log_debug_info(DEBUGL_CHECKS, 1, "Host is UP, so we'll retry the service check...\n");

				/* this is a soft state */
				temp_service->state_type = SOFT_STATE;

				/* log the service check retry */
				log_service_event(temp_service);
				alert_recorded = NEBATTR_CHECK_ALERT;

				/* run the service event handler to handle the soft state */
				handle_service_event(temp_service);

				if (reschedule_check == TRUE)
					next_service_check = next_check_time(temp_service);
			}

			/* perform dependency checks on the second to last check of the service */
			if (execute_service_checks && enable_predictive_service_dependency_checks == TRUE && temp_service->current_attempt == (temp_service->max_attempts - 1)) {
				objectlist *list;

				log_debug_info(DEBUGL_CHECKS, 1, "Looking for services to check for predictive dependency checks...\n");

				/* check services that THIS ONE depends on for notification AND execution */
				/* we do this because we might be sending out a notification soon and we want the dependency logic to be accurate */
				for (list = temp_service->exec_deps; list; list = list->next) {
					temp_dependency = (servicedependency *)list->object_ptr;
					if (temp_dependency->dependent_service_ptr == temp_service && temp_dependency->master_service_ptr != NULL) {
						master_service = (service *)temp_dependency->master_service_ptr;
						log_debug_info(DEBUGL_CHECKS, 2, "Predictive check of service '%s' on host '%s' queued.\n", master_service->description, master_service->host_name);
						schedule_service_check(master_service, current_time, CHECK_OPTION_DEPENDENCY_CHECK);
					}
				}
				for (list = temp_service->notify_deps; list; list = list->next) {
					temp_dependency = (servicedependency *)list->object_ptr;
					if (temp_dependency->dependent_service_ptr == temp_service && temp_dependency->master_service_ptr != NULL) {
						master_service = (service *)temp_dependency->master_service_ptr;
						log_debug_info(DEBUGL_CHECKS, 2, "Predictive check of service '%s' on host '%s' queued.\n", master_service->description, master_service->host_name);
						schedule_service_check(master_service, current_time, CHECK_OPTION_DEPENDENCY_CHECK);
					}
				}
			}
		}


		/* we've reached the maximum number of service rechecks, so handle the error */
		else {

			log_debug_info(DEBUGL_CHECKS, 1, "Service has reached max number of rechecks, so we'll handle the error...\n");

			/* this is a hard state */
			temp_service->state_type = HARD_STATE;

			/* if we've hard a hard state change... */
			if (hard_state_change == TRUE) {

				/* log the service problem (even if host is not up, which is new in 0.0.5) */
				log_service_event(temp_service);
				alert_recorded = NEBATTR_CHECK_ALERT;
			}

			/* else log the problem (again) if this service is flagged as being volatile */
			else if (temp_service->is_volatile == TRUE) {
				log_service_event(temp_service);
				alert_recorded = NEBATTR_CHECK_ALERT;
			}

			/* check for start of flexible (non-fixed) scheduled downtime if we just had a hard error */
			/* we need to check for both, state_change (SOFT) and hard_state_change (HARD) values */
			if ((hard_state_change == TRUE || state_change == TRUE) && temp_service->pending_flex_downtime > 0)
				check_pending_flex_service_downtime(temp_service);

			/* 10/04/07 check to see if the service and/or associate host is flapping */
			/* this should be done before a notification is sent out to ensure the host didn't just start flapping */
			check_for_service_flapping(temp_service, TRUE, TRUE);
			check_for_host_flapping(temp_host, TRUE, FALSE, TRUE);
			flapping_check_done = TRUE;

			/* (re)send notifications out about this service problem if the host is up (and was at last check also) and the dependencies were okay... */
			service_notification(temp_service, NOTIFICATION_NORMAL, NULL, NULL, NOTIFICATION_OPTION_NONE);

			/* run the service event handler if we changed state from the last hard state or if this service is flagged as being volatile */
			if (hard_state_change == TRUE || temp_service->is_volatile == TRUE)
				handle_service_event(temp_service);

			/* save the last hard state */
			temp_service->last_hard_state = temp_service->current_state;

			/* reschedule the next check at the regular interval */
			if (reschedule_check == TRUE)
				next_service_check = next_check_time(temp_service);
		}


		/* should we obsessive over service checks? */
		if (obsess_over_services == TRUE)
			obsessive_compulsive_service_check_processor(temp_service);
	}

	/* reschedule the next service check ONLY for active, scheduled checks */
	if (reschedule_check == TRUE && temp_service->check_interval != 0
	    && temp_service->checks_enabled)
	{
		temp_service->next_check = next_check_time(temp_service);
		log_debug_info(DEBUGL_CHECKS, 1, "Rescheduling next check of service at %s", ctime(&temp_service->next_check));
		temp_service->should_be_scheduled = TRUE;

		/* push the check to its next alotted time */
		temp_service->next_check = next_service_check;
		if (current_time > temp_service->next_check)
			temp_service->next_check = current_time;

		schedule_service_check(temp_service, temp_service->next_check, CHECK_OPTION_NONE);
	}

	/* if we're stalking this state type and state was not already logged AND the plugin output changed since last check, log it now.. */
	if (temp_service->state_type == HARD_STATE && state_change == FALSE && !alert_recorded && (compare_strings(old_plugin_output, temp_service->plugin_output) || compare_strings(old_long_plugin_output, temp_service->long_plugin_output))) {
		if (should_stalk(temp_service)) {
			log_service_event(temp_service);
			alert_recorded = NEBATTR_CHECK_ALERT;
		}
	}

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_service_check(
		NEBTYPE_SERVICECHECK_PROCESSED,
		NEBFLAG_NONE,
		alert_recorded | first_recorded_state,
		temp_service,
		temp_service->check_type,
		queued_check_result->start_time,
		queued_check_result->finish_time,
		NULL,
		temp_service->latency,
		temp_service->execution_time,
		service_check_timeout,
		queued_check_result->early_timeout,
		queued_check_result->return_code,
		NULL,
		NULL,
		queued_check_result);
#endif

	/* set the checked flag */
	temp_service->has_been_checked = TRUE;

	/* update the current service status log */
	update_service_status(temp_service, FALSE);

	/* check to see if the service and/or associate host is flapping */
	if (flapping_check_done == FALSE) {
		check_for_service_flapping(temp_service, TRUE, TRUE);
		check_for_host_flapping(temp_host, TRUE, FALSE, TRUE);
	}

	/* update service performance info */
	update_service_performance_data(temp_service);

	/* free allocated memory */
	my_free(temp_plugin_output);
	my_free(old_plugin_output);
	my_free(old_long_plugin_output);

	return OK;
}


/* schedules an immediate or delayed service check */
void schedule_service_check(service *svc, time_t check_time, int options)
{
	timed_event *temp_event = NULL;
	time_t next_event_time = 0;
	int use_original_event = TRUE;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "schedule_service_check()\n");

	if (svc == NULL)
		return;

	log_debug_info(DEBUGL_CHECKS, 0, "Scheduling a %s, active check of service '%s' on host '%s' @ %s", (options & CHECK_OPTION_FORCE_EXECUTION) ? "forced" : "non-forced", svc->description, svc->host_name, ctime(&check_time));

	/* don't schedule a check if active checks of this service are disabled */
	if (svc->checks_enabled == FALSE && !(options & CHECK_OPTION_FORCE_EXECUTION)) {
		log_debug_info(DEBUGL_CHECKS, 0, "Active checks of this service are disabled.\n");
		return;
	}

	/* we may have to nudge this check a bit */
	if (options == CHECK_OPTION_DEPENDENCY_CHECK) {
		if (svc->last_check + cached_service_check_horizon > check_time) {
			log_debug_info(DEBUGL_CHECKS, 0, "Last check result is recent enough (%s)", ctime(&svc->last_check));
			return;
		}
	}

	/* default is to use the new event */
	use_original_event = FALSE;

	temp_event = (timed_event *)svc->next_check_event;
	/*
	 * If the service already has a check scheduled,
	 * we need to decide which of the events to use
	 */
	if (temp_event != NULL) {
		next_event_time = temp_event->run_time; /* FIXME */

		log_debug_info(DEBUGL_CHECKS, 2, "Found another service check event for this service @ %s", ctime(&next_event_time));

		/* use the originally scheduled check unless we decide otherwise */
		use_original_event = TRUE;

		/* the original event is a forced check... */
		if ((svc->check_options & CHECK_OPTION_FORCE_EXECUTION)) {

			/* the new event is also forced and its execution time is earlier than the original, so use it instead */
			if ((options & CHECK_OPTION_FORCE_EXECUTION) && (check_time < next_event_time)) {
				use_original_event = FALSE;
				log_debug_info(DEBUGL_CHECKS, 2, "New service check event is forced and occurs before the existing event, so the new event will be used instead.\n");
			}
		}

		/* the original event is not a forced check... */
		else {

			/* the new event is a forced check, so use it instead */
			if ((options & CHECK_OPTION_FORCE_EXECUTION)) {
				use_original_event = FALSE;
				log_debug_info(DEBUGL_CHECKS, 2, "New service check event is forced, so it will be used instead of the existing event.\n");
			}

			/* the new event is not forced either and its execution time is earlier than the original, so use it instead */
			else if (check_time < next_event_time) {
				use_original_event = FALSE;
				log_debug_info(DEBUGL_CHECKS, 2, "New service check event occurs before the existing (older) event, so it will be used instead.\n");
			}

			/* the new event is older, so override the existing one */
			else {
				log_debug_info(DEBUGL_CHECKS, 2, "New service check event occurs after the existing event, so we'll ignore it.\n");
			}
		}
	}

	/* schedule a new event */
	if (use_original_event == FALSE) {
		/* make sure we remove the old event from the queue */
		if (temp_event) {
			destroy_event(temp_event);
			temp_event = NULL;
			svc->next_check_event = NULL;
		}

		log_debug_info(DEBUGL_CHECKS, 2, "Scheduling new service check event.\n");

		/* set the next service check event and time */
		svc->next_check = check_time;

		/* save check options for retention purposes */
		svc->check_options = options;

		svc->next_check_event = schedule_event(svc->next_check - time(NULL), handle_service_check_event, (void*)svc);
	}

	else {
		/* reset the next check time (it may be out of sync) */
		if (temp_event != NULL)
			svc->next_check = temp_event->run_time;

		log_debug_info(DEBUGL_CHECKS, 2, "Keeping original service check event (ignoring the new one).\n");
	}


	/* update the status log */
	update_service_status(svc, FALSE);

	return;
}


/* checks viability of performing a service check */
static int check_service_check_viability(service *svc, int check_options, int *time_is_valid, time_t *new_time)
{
	int result = OK;
	int perform_check = TRUE;
	time_t current_time = 0L;
	time_t preferred_time = 0L;
	int check_interval = 0;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "check_service_check_viability()\n");

	/* make sure we have a service */
	if (svc == NULL)
		return ERROR;

	/* get the check interval to use if we need to reschedule the check */
	if (svc->state_type == SOFT_STATE && svc->current_state != STATE_OK)
		check_interval = (svc->retry_interval * interval_length);
	else
		check_interval = (svc->check_interval * interval_length);

	/* get the current time */
	time(&current_time);

	/* initialize the next preferred check time */
	preferred_time = current_time;

	/* can we check the host right now? */
	if (!(check_options & CHECK_OPTION_FORCE_EXECUTION)) {

		/* if checks of the service are currently disabled... */
		if (svc->checks_enabled == FALSE) {
			preferred_time = current_time + check_interval;
			perform_check = FALSE;

			log_debug_info(DEBUGL_CHECKS, 2, "Active checks of the service are currently disabled.\n");
		}

		/* make sure this is a valid time to check the service */
		if (check_time_against_period((unsigned long)current_time, svc->check_period_ptr) == ERROR) {
			preferred_time = current_time;
			if (time_is_valid)
				*time_is_valid = FALSE;
			perform_check = FALSE;

			log_debug_info(DEBUGL_CHECKS, 2, "This is not a valid time for this service to be actively checked.\n");
		}

		/* check service dependencies for execution */
		if (check_service_dependencies(svc, EXECUTION_DEPENDENCY) == DEPENDENCIES_FAILED) {
			preferred_time = current_time + check_interval;
			perform_check = FALSE;

			log_debug_info(DEBUGL_CHECKS, 2, "Execution dependencies for this service failed, so it will not be actively checked.\n");
		}
	}

	/* pass back the next viable check time */
	if (new_time)
		*new_time = preferred_time;

	result = (perform_check == TRUE) ? OK : ERROR;

	return result;
}


/* checks service dependencies */
int check_service_dependencies(service *svc, int dependency_type)
{
	objectlist *list;
	int state = STATE_OK;
	time_t current_time = 0L;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "check_service_dependencies()\n");

	/* only check dependencies of the desired type */
	if (dependency_type == NOTIFICATION_DEPENDENCY)
		list = svc->notify_deps;
	else
		list = svc->exec_deps;

	/* check all dependencies of the desired type... */
	for (; list; list = list->next) {
		service *temp_service;
		servicedependency *temp_dependency = (servicedependency *)list->object_ptr;

		/* find the service we depend on... */
		if ((temp_service = temp_dependency->master_service_ptr) == NULL)
			continue;

		/* skip this dependency if it has a timeperiod and the current time isn't valid */
		time(&current_time);
		if (temp_dependency->dependency_period != NULL && check_time_against_period(current_time, temp_dependency->dependency_period_ptr) == ERROR)
			return FALSE;

		/* get the status to use (use last hard state if its currently in a soft state) */
		if (temp_service->state_type == SOFT_STATE && soft_state_dependencies == FALSE)
			state = temp_service->last_hard_state;
		else
			state = temp_service->current_state;

		/* is the service we depend on in state that fails the dependency tests? */
		if (flag_isset(temp_dependency->failure_options, 1 << state))
			return DEPENDENCIES_FAILED;

		/* immediate dependencies ok at this point - check parent dependencies if necessary */
		if (temp_dependency->inherits_parent == TRUE) {
			if (check_service_dependencies(temp_service, dependency_type) != DEPENDENCIES_OK)
				return DEPENDENCIES_FAILED;
		}
	}

	return DEPENDENCIES_OK;
}


/* check for services that never returned from a check... */
void check_for_orphaned_services(void)
{
	service *temp_service = NULL;
	time_t current_time = 0L;
	time_t expected_time = 0L;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "check_for_orphaned_services()\n");

	/* get the current time */
	time(&current_time);

	/* check all services... */
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		/* skip services that are not currently executing */
		if (temp_service->is_executing == FALSE)
			continue;

		/* determine the time at which the check results should have come in (allow 10 minutes slack time) */
		expected_time = (time_t)(temp_service->next_check + temp_service->latency + service_check_timeout + check_reaper_interval + 600);

		/* this service was supposed to have executed a while ago, but for some reason the results haven't come back in... */
		if (expected_time < current_time) {

			/* log a warning */
			nm_log(NSLOG_RUNTIME_WARNING,
			       "Warning: The check of service '%s' on host '%s' looks like it was orphaned (results never came back; last_check=%lu; next_check=%lu).  I'm scheduling an immediate check of the service...\n", temp_service->description, temp_service->host_name, temp_service->last_check, temp_service->next_check);

			log_debug_info(DEBUGL_CHECKS, 1, "Service '%s' on host '%s' was orphaned, so we're scheduling an immediate check...\n", temp_service->description, temp_service->host_name);
			log_debug_info(DEBUGL_CHECKS, 1, "  next_check=%lu (%s); last_check=%lu (%s);\n",
			               temp_service->next_check, ctime(&temp_service->next_check),
			               temp_service->last_check, ctime(&temp_service->last_check));

			/* decrement the number of running service checks */
			if (currently_running_service_checks > 0)
				currently_running_service_checks--;

			/* disable the executing flag */
			temp_service->is_executing = FALSE;

			/* schedule an immediate check of the service */
			schedule_service_check(temp_service, current_time, CHECK_OPTION_ORPHAN_CHECK);
		}

	}

	return;
}


/* event handler for checking freshness of service results */
static void check_service_result_freshness(void *arg)
{
	service *temp_service = NULL;
	time_t current_time = 0L;

	/* get the current time */
	time(&current_time);

	schedule_event(service_freshness_check_interval, check_service_result_freshness, NULL);


	log_debug_info(DEBUGL_FUNCTIONS, 0, "check_service_result_freshness()\n");
	log_debug_info(DEBUGL_CHECKS, 1, "Checking the freshness of service check results...\n");

	/* bail out if we're not supposed to be checking freshness */
	if (check_service_freshness == FALSE) {
		log_debug_info(DEBUGL_CHECKS, 1, "Service freshness checking is disabled.\n");
		return;
	}

	/* check all services... */
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		/* skip services we shouldn't be checking for freshness */
		if (temp_service->check_freshness == FALSE)
			continue;

		/* skip services that are currently executing (problems here will be caught by orphaned service check) */
		if (temp_service->is_executing == TRUE)
			continue;

		/* skip services that have both active and passive checks disabled */
		if (temp_service->checks_enabled == FALSE && temp_service->accept_passive_checks == FALSE)
			continue;

		/* skip services that are already being freshened */
		if (temp_service->is_being_freshened == TRUE)
			continue;

		/* see if the time is right... */
		if (check_time_against_period(current_time, temp_service->check_period_ptr) == ERROR)
			continue;

		/* EXCEPTION */
		/* don't check freshness of services without regular check intervals if we're using auto-freshness threshold */
		if (temp_service->check_interval == 0 && temp_service->freshness_threshold == 0)
			continue;

		/* the results for the last check of this service are stale! */
		if (is_service_result_fresh(temp_service, current_time, TRUE) == FALSE) {

			/* set the freshen flag */
			temp_service->is_being_freshened = TRUE;

			/* schedule an immediate forced check of the service */
			schedule_service_check(temp_service, current_time, CHECK_OPTION_FORCE_EXECUTION | CHECK_OPTION_FRESHNESS_CHECK);
		}

	}

	return;
}


/* tests whether or not a service's check results are fresh */
static int is_service_result_fresh(service *temp_service, time_t current_time, int log_this)
{
	int freshness_threshold = 0;
	time_t expiration_time = 0L;
	int days = 0;
	int hours = 0;
	int minutes = 0;
	int seconds = 0;
	int tdays = 0;
	int thours = 0;
	int tminutes = 0;
	int tseconds = 0;

	log_debug_info(DEBUGL_CHECKS, 2, "Checking freshness of service '%s' on host '%s'...\n", temp_service->description, temp_service->host_name);

	/* use user-supplied freshness threshold or auto-calculate a freshness threshold to use? */
	if (temp_service->freshness_threshold == 0) {
		if (temp_service->state_type == HARD_STATE || temp_service->current_state == STATE_OK)
			freshness_threshold = (temp_service->check_interval * interval_length) + temp_service->latency + additional_freshness_latency;
		else
			freshness_threshold = (temp_service->retry_interval * interval_length) + temp_service->latency + additional_freshness_latency;
	} else
		freshness_threshold = temp_service->freshness_threshold;

	log_debug_info(DEBUGL_CHECKS, 2, "Freshness thresholds: service=%d, use=%d\n", temp_service->freshness_threshold, freshness_threshold);

	/* calculate expiration time */
	/*
	 * CHANGED 11/10/05 EG -
	 * program start is only used in expiration time calculation
	 * if > last check AND active checks are enabled, so active checks
	 * can become stale immediately upon program startup
	 */
	/*
	 * CHANGED 02/25/06 SG -
	 * passive checks also become stale, so remove dependence on active
	 * check logic
	 */
	if (temp_service->has_been_checked == FALSE)
		expiration_time = (time_t)(event_start + freshness_threshold);
	/*
	 * CHANGED 06/19/07 EG -
	 * Per Ton's suggestion (and user requests), only use program start
	 * time over last check if no specific threshold has been set by user.
	 * Problems can occur if Nagios is restarted more frequently that
	 * freshness threshold intervals (services never go stale).
	 */
	/*
	 * CHANGED 10/07/07 EG:
	 * Only match next condition for services that
	 * have active checks enabled...
	 */
	else if (temp_service->checks_enabled == TRUE && event_start > temp_service->last_check && temp_service->freshness_threshold == 0)
		expiration_time = (time_t)(event_start + freshness_threshold);
	else
		expiration_time = (time_t)(temp_service->last_check + freshness_threshold);

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
	if (temp_service->check_type == CHECK_TYPE_PASSIVE) {
		if (temp_service->last_check < event_start &&
		    event_start - last_program_stop > freshness_threshold * 0.618) {
			expiration_time = event_start + freshness_threshold;
		}
	}
	log_debug_info(DEBUGL_CHECKS, 2, "HBC: %d, PS: %lu, ES: %lu, LC: %lu, CT: %lu, ET: %lu\n", temp_service->has_been_checked, (unsigned long)program_start, (unsigned long)event_start, (unsigned long)temp_service->last_check, (unsigned long)current_time, (unsigned long)expiration_time);

	/* the results for the last check of this service are stale */
	if (expiration_time < current_time) {

		get_time_breakdown((current_time - expiration_time), &days, &hours, &minutes, &seconds);
		get_time_breakdown(freshness_threshold, &tdays, &thours, &tminutes, &tseconds);

		/* log a warning */
		if (log_this == TRUE)
			nm_log(NSLOG_RUNTIME_WARNING,
			       "Warning: The results of service '%s' on host '%s' are stale by %dd %dh %dm %ds (threshold=%dd %dh %dm %ds).  I'm forcing an immediate check of the service.\n", temp_service->description, temp_service->host_name, days, hours, minutes, seconds, tdays, thours, tminutes, tseconds);

		log_debug_info(DEBUGL_CHECKS, 1, "Check results for service '%s' on host '%s' are stale by %dd %dh %dm %ds (threshold=%dd %dh %dm %ds).  Forcing an immediate check of the service...\n", temp_service->description, temp_service->host_name, days, hours, minutes, seconds, tdays, thours, tminutes, tseconds);

		return FALSE;
	}

	log_debug_info(DEBUGL_CHECKS, 1, "Check results for service '%s' on host '%s' are fresh.\n", temp_service->description, temp_service->host_name);

	return TRUE;
}
