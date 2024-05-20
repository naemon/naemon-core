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
#include "objects_servicedependency.h"
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "neberrors.h"

/* Scheduling (before worker job is started) */
static void handle_service_check_event(struct nm_event_execution_properties *evprop);

/* Check exeuction */
static int run_scheduled_service_check(service *, int, double);

/* Result handling (After worker job is executed) */
static void handle_worker_service_check(wproc_result *wpres, void *arg, int flags);

/* Extra features */
static void check_service_result_freshness(struct nm_event_execution_properties *evprop);
static void check_for_orphaned_services_eventhandler(struct nm_event_execution_properties *evprop);

/* Status functions, immutable */
static int is_service_result_fresh(service *, time_t, int);


/******************************************************************************
 *******************************  INIT METHODS  *******************************
 ******************************************************************************/

void checks_init_services(void)
{
	service *temp_service = NULL;
	time_t delay;
	time_t current_time = time(NULL);

	log_debug_info(DEBUGL_EVENTS, 2, "Scheduling service checks...\n");

	/* add scheduled service checks to event queue */
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		/* update status of all services (scheduled or not) */
		update_service_status(temp_service, FALSE);

		/* Determine the delay used for the first check event.
		 * If use_retained_scheduling_info is enabled, we use the previously set
		 * next_check. If the check was missed, schedule it within the next
		 * retained_scheduling_randomize_window. If more than one check was missed, we schedule the check
		 * randomly instead. If the next_check is more than one check_interval in
		 * the future, we also schedule the next check randomly. This indicates
		 * that the check_interval has been lowered over restarts.
		 */
		if (use_retained_scheduling_info == TRUE &&
		    temp_service->next_check > current_time - get_service_check_interval_s(temp_service) &&
		    temp_service->next_check <= current_time + get_service_check_interval_s(temp_service)) {
			if (temp_service->next_check < current_time) {
				int scheduling_window = retained_scheduling_randomize_window;
				if (retained_scheduling_randomize_window > get_service_check_interval_s(temp_service)) {
					scheduling_window = get_service_check_interval_s(temp_service);
				}
				delay = ranged_urand(0, scheduling_window);
			} else {
				delay = temp_service->next_check - current_time;
			}
		} else {
			delay = ranged_urand(0, get_service_check_interval_s(temp_service));
		}

		/* create a new service check event */
		if (temp_service->check_interval != 0.0)
			schedule_next_service_check(temp_service, delay, 0);
	}

	/* add a service result "freshness" check event */
	if (check_service_freshness == TRUE) {
		schedule_event(service_freshness_check_interval, check_service_result_freshness, NULL);
	}

	if (check_orphaned_services == TRUE) {
		schedule_event(DEFAULT_ORPHAN_CHECK_INTERVAL, check_for_orphaned_services_eventhandler, NULL);
	}
}


/******************************************************************************
 ********************************  SCHEDULING  ********************************
 ******************************************************************************/


void schedule_next_service_check(service *svc, time_t delay, int options)
{
	time_t current_time = time(NULL);

	/* A closer check is already scheduled, skip this scheduling */
	if (svc->next_check_event != NULL && svc->next_check < delay + current_time) {
		/*... unless this is a forced check or postponement is allowed*/
		if (!(options & (CHECK_OPTION_FORCE_EXECUTION | CHECK_OPTION_ALLOW_POSTPONE))) {
			return;
		}
	}

	/* We have a scheduled check, drop that event to make space for the new event */
	if (svc->next_check_event != NULL) {
		destroy_event(svc->next_check_event);
	}

	/* Schedule the event */
	svc->check_options = options;
	svc->next_check = delay + current_time;
	svc->last_update = current_time;
	svc->next_check_event = schedule_event(delay, handle_service_check_event, (void *)svc);

	/* update the status log, since next_check and check_options is updated */
	update_service_status(svc, FALSE);
}

/* schedules an immediate or delayed service check */
void schedule_service_check(service *svc, time_t check_time, int options)
{
	schedule_next_service_check(svc, check_time - time(NULL), options);
}

static void handle_service_check_event(struct nm_event_execution_properties *evprop)
{
	service *temp_service = (service *)evprop->user_data;
	int nudge_seconds = 0;
	double latency;
	struct timeval tv;
	int options = temp_service->check_options;
	host *temp_host = NULL;

	log_debug_info(DEBUGL_CHECKS, 0, "Service '%s' on host '%s' handle_service_check_event()...\n", temp_service->description, temp_service->host_name);

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {

		/* get event latency */
		latency = evprop->attributes.timed.latency;
		gettimeofday(&tv, NULL);

		/* When the callback is called, the pointer to the timed event is invalid */
		temp_service->next_check_event = NULL;

		/* Reschedule next check directly, might be replaced later */
		if (temp_service->check_interval != 0.0 && temp_service->is_executing == FALSE) {
			schedule_next_service_check(temp_service, get_service_check_interval_s(temp_service), 0);
		}

		/* forced checks override normal check logic */
		if (!(options & CHECK_OPTION_FORCE_EXECUTION)) {
			/* don't run a service check if we're already maxed out on the number of parallel service checks...  */
			if (max_parallel_service_checks != 0 && (currently_running_service_checks >= max_parallel_service_checks)) {
				nm_log(NSLOG_RUNTIME_WARNING,
				       "\tMax concurrent service checks (%d) has been reached.  Nudging %s:%s by %d seconds...\n", max_parallel_service_checks, temp_service->host_name, temp_service->description, nudge_seconds);
				/* Simply reschedule at retry_interval instead, if defined (otherwise keep scheduling at normal interval) */
				if (temp_service->retry_interval != 0.0 && temp_service->is_executing == FALSE) {
					schedule_next_service_check(temp_service, get_service_retry_interval_s(temp_service), 0);
				}
				return;
			}

			/* don't run a service check if active checks are disabled */
			if (execute_service_checks == FALSE) {
				return;
			}

			/* Don't execute check if already executed close enough */
			if (temp_service->last_check + cached_service_check_horizon > tv.tv_sec && temp_service->last_check <= tv.tv_sec) {
				log_debug_info(DEBUGL_CHECKS, 0, "Service '%s' on host '%s' was last checked within its cache horizon. Aborting check\n", temp_service->description, temp_service->host_name);
				return;
			}

			/* if checks of the service are currently disabled... */
			if (temp_service->checks_enabled == FALSE) {
				return;
			}

			/* make sure this is a valid time to check the service */
			if (check_time_against_period(time(NULL), temp_service->check_period_ptr) == ERROR) {
				return;
			}

			/* check service dependencies for execution */
			log_debug_info(DEBUGL_CHECKS, 0, "Service '%s' on host '%s' checking dependencies...\n", temp_service->description, temp_service->host_name);
			if (check_service_dependencies(temp_service, EXECUTION_DEPENDENCY) == DEPENDENCIES_FAILED) {
				int keep_running = FALSE;
				switch(service_skip_check_dependency_status) {
					case SKIP_KEEP_RUNNING_WHEN_UP:
						if (temp_service->current_state <= STATE_WARNING) {
							keep_running = TRUE;
						}
						break;
					case STATE_OK:
					case STATE_WARNING:
					case STATE_CRITICAL:
					case STATE_UNKNOWN:
						temp_service->current_state = service_skip_check_dependency_status;
						if (strstr(temp_service->plugin_output, "(service dependency check failed)") == NULL) {
							char *old_output = nm_strdup(temp_service->plugin_output);
							nm_free(temp_service->plugin_output);
							nm_asprintf(&temp_service->plugin_output, "(service dependency check failed) was: %s", old_output);
							nm_free(old_output);
						}
						break;
				}
				if (!keep_running) {
					log_debug_info(DEBUGL_CHECKS, 0, "Service '%s' on host '%s' failed dependency check. Aborting check\n", temp_service->description, temp_service->host_name);
					return;
				}
			}

			/* check service parents for execution */
			if(service_parents_disable_service_checks && temp_service->parents) {
				int parents_failed = FALSE;
				if (temp_service->current_state != STATE_OK) {
					servicesmember *sm = temp_service->parents;
					while (sm && sm->service_ptr->current_state != STATE_OK) {
						sm = sm->next;
					}
					if (sm == NULL) {
						parents_failed = TRUE;
					}
				}
				if(parents_failed) {
					switch(service_skip_check_dependency_status) {
						case SKIP_KEEP_RUNNING_WHEN_UP:
							if (temp_service->current_state <= STATE_WARNING) {
								parents_failed = FALSE;
							}
							break;
						case STATE_OK:
						case STATE_WARNING:
						case STATE_CRITICAL:
						case STATE_UNKNOWN:
							temp_service->current_state = service_skip_check_dependency_status;
							if (strstr(temp_service->plugin_output, "(service parents failed)") == NULL) {
								char *old_output = nm_strdup(temp_service->plugin_output);
								nm_free(temp_service->plugin_output);
								nm_asprintf(&temp_service->plugin_output, "(service parents failed) was: %s", old_output);
								nm_free(old_output);
							}
							break;
					}
				}
				if(parents_failed) {
					log_debug_info(DEBUGL_CHECKS, 0, "Service '%s' on host '%s' failed parents check. Aborting check\n", temp_service->description, temp_service->host_name);
					return;
				}
			}


			/* check if host is up - if not, do not perform check */
			if (host_down_disable_service_checks) {
				if ((temp_host = temp_service->host_ptr) == NULL) {
					log_debug_info(DEBUGL_CHECKS, 2, "Host pointer NULL in handle_service_check_event().\n");
					return;
				}
				if (temp_host->current_state != STATE_UP) {
					int keep_running = TRUE;
					switch (service_skip_check_host_down_status) {
					/* only keep running if service is up or host_down_disable_service_checks is disabled */
					case SKIP_KEEP_RUNNING_WHEN_UP:
						if (temp_service->current_state > STATE_WARNING) {
							log_debug_info(DEBUGL_CHECKS, 2, "Host and service state not UP, so service check will not be performed - will be rescheduled as normal.\n");
							keep_running = FALSE;
						}
						break;
					default:
						log_debug_info(DEBUGL_CHECKS, 2, "Host state not UP, so service check will not be performed - will be rescheduled as normal.\n");
						keep_running = FALSE;
						break;
					}
					if(!keep_running) {
						if (service_skip_check_host_down_status >= 0) {
							temp_service->current_state = service_skip_check_host_down_status;
							if (strstr(temp_service->plugin_output, "(host is down)") == NULL) {
								char *old_output = nm_strdup(temp_service->plugin_output);
								nm_free(temp_service->plugin_output);
								nm_asprintf(&temp_service->plugin_output, "(host is down) was: %s", old_output);
								nm_free(old_output);
							}
						}
						return;
					}
				}
			}
		}

		/* Otherwise, run the event */
		run_scheduled_service_check(temp_service, options, latency);
	} else if (evprop->execution_type == EVENT_EXEC_ABORTED) {
		/* If the event is destroyed, remove the reference. */
		temp_service->next_check_event = NULL;
	}
}


/******************************************************************************
 *****************************  CHECK EXECUTION  ******************************
 ******************************************************************************/

/* forks a child process to run a service check, but does not wait for the service check result */
static int run_scheduled_service_check(service *svc, int check_options, double latency)
{
	nagios_macros mac;
	char *raw_command = NULL;
	char *processed_command = NULL;
	struct timeval start_time, end_time;
	host *temp_host = NULL;
	check_result *cr;
	int runchk_result = OK;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	int neb_result = OK;

	/* latency is how long the event lagged behind the event queue */
	svc->latency = latency;

	temp_host = svc->host_ptr;

	/******** GOOD TO GO FOR A REAL SERVICE CHECK AT THIS POINT ********/

	/* initialize start/end times */
	start_time.tv_sec = 0L;
	start_time.tv_usec = 0L;
	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;

	neb_result = broker_service_check(NEBTYPE_SERVICECHECK_ASYNC_PRECHECK, NEBFLAG_NONE, NEBATTR_NONE, svc, CHECK_TYPE_ACTIVE, start_time, end_time, svc->check_command, svc->latency, 0.0, 0, FALSE, 0, NULL, NULL);

	if (neb_result == NEBERROR_CALLBACKCANCEL || neb_result == NEBERROR_CALLBACKOVERRIDE) {
		log_debug_info(DEBUGL_CHECKS, 0, "Check of service '%s' on host '%s' (id=%u) was %s by a module\n",
		               svc->description, svc->host_name, svc->id,
		               neb_result == NEBERROR_CALLBACKCANCEL ? "cancelled" : "overridden");
	}

	/* get the command start time */
	gettimeofday(&start_time, NULL);
	svc->last_update = start_time.tv_sec;

	/* neb module wants to cancel the service check - the check will be rescheduled for a later time by the scheduling logic */
	if (neb_result == NEBERROR_CALLBACKCANCEL) {
		return ERROR;
	}

	/* neb module wants to override (or cancel) the service check - perhaps it will check the service itself */
	/* NOTE: if a module does this, it has to do a lot of the stuff found below to make sure things don't get whacked out of shape! */
	/* NOTE: if would be easier for modules to override checks when the NEBTYPE_SERVICECHECK_INITIATE event is called (later) */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE)
		return OK;


	log_debug_info(DEBUGL_CHECKS, 0, "Checking service '%s' on host '%s'...\n", svc->description, svc->host_name);

	/* grab the host and service macro variables */
	memset(&mac, 0, sizeof(mac));
	grab_host_macros_r(&mac, temp_host);
	grab_service_macros_r(&mac, svc);

	get_raw_command_line_r(&mac, svc->check_command_ptr, svc->check_command, &raw_command, macro_options);
	if (raw_command == NULL) {
		clear_volatile_macros_r(&mac);
		log_debug_info(DEBUGL_CHECKS, 0, "Raw check command for service '%s' on host '%s' was NULL - aborting.\n", svc->description, svc->host_name);
		return ERROR;
	}

	/* process any macros contained in the argument */
	process_macros_r(&mac, raw_command, &processed_command, macro_options);
	nm_free(raw_command);
	if (processed_command == NULL) {
		clear_volatile_macros_r(&mac);
		log_debug_info(DEBUGL_CHECKS, 0, "Processed check command for service '%s' on host '%s' was NULL - aborting.\n", svc->description, svc->host_name);
		return ERROR;
	}

	cr = nm_calloc(1, sizeof(*cr));
	init_check_result(cr);

	/* save check info */
	cr->object_check_type = SERVICE_CHECK;
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
	cr->host_name = nm_strdup(svc->host_name);
	cr->service_description = nm_strdup(svc->description);

	neb_result = broker_service_check(NEBTYPE_SERVICECHECK_INITIATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CHECK_TYPE_ACTIVE, start_time, end_time, svc->check_command, svc->latency, 0.0, service_check_timeout, FALSE, 0, processed_command, cr);

	/* neb module wants to override the service check - perhaps it will check the service itself */
	if (neb_result == NEBERROR_CALLBACKOVERRIDE || neb_result == NEBERROR_CALLBACKCANCEL) {
		clear_volatile_macros_r(&mac);
		free_check_result(cr);
		nm_free(cr);
		nm_free(processed_command);
		return neb_result == NEBERROR_CALLBACKOVERRIDE ? OK : ERROR;
	}

	/* paw off the check to a worker to run */
	runchk_result = wproc_run_callback(processed_command, service_check_timeout, handle_worker_service_check, (void *)cr, &mac);
	if (runchk_result == ERROR) {
		nm_log(NSLOG_RUNTIME_ERROR,
		       "Unable to send check for service '%s' on host '%s' to worker (ret=%d)\n", svc->description, svc->host_name, runchk_result);
	} else {
		/* do the book-keeping */
		currently_running_service_checks++;
		svc->is_executing = TRUE;
		update_check_stats(ACTIVE_SCHEDULED_SERVICE_CHECK_STATS, start_time.tv_sec);
	}

	nm_free(processed_command);
	clear_volatile_macros_r(&mac);

	return OK;
}


/******************************************************************************
 *****************************  RESULT HANDLING  ******************************
 ******************************************************************************/


static void handle_worker_service_check(wproc_result *wpres, void *arg, int flags)
{
	check_result *cr = (check_result *)arg;
	if (wpres) {
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
	nm_free(cr);
}


/* handles asynchronous service check results */
int handle_async_service_check_result(service *temp_service, check_result *queued_check_result)
{
	host *temp_host = NULL;
	int state_change = FALSE;
	int hard_state_change = FALSE;
	int first_host_check_initiated = FALSE;
	int route_result = STATE_UP;
	time_t current_time = 0L;
	int alert_recorded = NEBATTR_NONE;
	int first_recorded_state = NEBATTR_NONE;
	char *old_plugin_output = NULL;
	char *old_long_plugin_output = NULL;
	char *temp_ptr = NULL;
	servicedependency *temp_dependency = NULL;
	service *master_service = NULL;
	int state_changes_use_cached_state = TRUE; /* TODO - 09/23/07 move this to a global variable */
	int flapping_check_done = FALSE;

	/* make sure we have what we need */
	if (temp_service == NULL || queued_check_result == NULL)
		return ERROR;

	/* get the current time */
	time(&current_time);

	log_debug_info(DEBUGL_CHECKS, 0, "** Handling check result for service '%s' on host '%s' from '%s'...\n", temp_service->description, temp_service->host_name, check_result_source(queued_check_result));
	log_debug_info(DEBUGL_CHECKS, 1, "HOST: %s, SERVICE: %s, CHECK TYPE: %s, OPTIONS: %d, SCHEDULED: %s, EXITED OK: %s, RETURN CODE: %d, OUTPUT: %s\n", temp_service->host_name, temp_service->description, (queued_check_result->check_type == CHECK_TYPE_ACTIVE) ? "Active" : "Passive", queued_check_result->check_options, (queued_check_result->scheduled_check == TRUE) ? "Yes" : "No", (queued_check_result->exited_ok == TRUE) ? "Yes" : "No", queued_check_result->return_code, queued_check_result->output);

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

	temp_service->last_update = current_time;

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

	temp_service->latency = queued_check_result->latency;

	/* update check statistics for passive checks */
	if (queued_check_result->check_type == CHECK_TYPE_PASSIVE)
		update_check_stats(PASSIVE_SERVICE_CHECK_STATS, queued_check_result->start_time.tv_sec);

	/* save the old service status info */
	temp_service->last_state = temp_service->current_state;

	/* save old plugin output */
	if (temp_service->plugin_output)
		old_plugin_output = nm_strdup(temp_service->plugin_output);

	if (temp_service->long_plugin_output)
		old_long_plugin_output = strdup(temp_service->long_plugin_output);
	/* clear the old plugin output and perf data buffers */
	nm_free(temp_service->plugin_output);
	nm_free(temp_service->long_plugin_output);
	nm_free(temp_service->perf_data);

	if (queued_check_result->early_timeout == TRUE) {
		nm_asprintf(&temp_service->plugin_output, "(Service check timed out after %.2lf seconds)", temp_service->execution_time);
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
		if (temp_host->has_been_checked == FALSE && (temp_host->initial_state != STATE_UP || (unsigned long)temp_host->next_check == 0L || (unsigned long)(temp_host->next_check - current_time) > 300)) {

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

		/* reset notification times */
		temp_service->last_notification = (time_t)0;
		temp_service->next_notification = (time_t)0;

		/* reset notification suppression option */
		temp_service->no_more_notifications = FALSE;

		if (temp_service->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL && (state_change == TRUE || hard_state_change == FALSE)) {

			temp_service->problem_has_been_acknowledged = FALSE;
			temp_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
			temp_service->acknowledgement_end_time = (time_t)0;

			/* remove any non-persistant comments associated with the ack */
			delete_service_acknowledgement_comments(temp_service);
		} else if (temp_service->acknowledgement_type == ACKNOWLEDGEMENT_STICKY && temp_service->current_state == STATE_OK) {

			temp_service->problem_has_been_acknowledged = FALSE;
			temp_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
			temp_service->acknowledgement_end_time = (time_t)0;

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
			nm_free(temp_service->current_problem_id);
			temp_service->current_problem_id = (char*)g_uuid_string_random();
			temp_service->problem_start = current_time;
			temp_service->problem_end = 0L;
		}

		/* clear the problem id when transitioning from a problem state to an OK state */
		if (temp_service->current_state == STATE_OK) {
			temp_service->last_problem_id = temp_service->current_problem_id;
			temp_service->current_problem_id = NULL;
			if(temp_service->problem_start > 0)
				temp_service->problem_end = current_time;
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
		temp_service->acknowledgement_end_time = (time_t)0;

		/* verify the route to the host and send out host recovery notifications */
		if (temp_host->current_state != STATE_UP) {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is NOT UP, so we'll check it to see if it recovered...\n");

			if (first_host_check_initiated == TRUE)
				log_debug_info(DEBUGL_CHECKS, 1, "First host check was already initiated, so we'll skip a new host check.\n");
			else {
				/* can we use the last cached host state? */
				/* usually only use cached host state if no service state change has occurred */
				if ((state_change == FALSE || state_changes_use_cached_state == TRUE) && temp_host->has_been_checked == TRUE && (temp_host->last_check + cached_host_check_horizon > current_time && temp_host->last_check <= current_time)) {
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
			check_for_service_flapping(temp_service, TRUE);
			check_for_host_flapping(temp_host, TRUE, FALSE);
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
		temp_service->acknowledgement_end_time = (time_t)0;
		temp_service->notified_on = 0;
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
		if (temp_host->current_state == STATE_UP) {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is currently UP, so we'll recheck its state to make sure...\n");

			/* only run a new check if we can and have to */
			if (!execute_host_checks || (temp_host->last_check + cached_host_check_horizon > current_time && temp_host->last_check <= current_time)) {
				log_debug_info(DEBUGL_CHECKS, 1, "* Using cached host state: %d\n", temp_host->current_state);
				update_check_stats(ACTIVE_ONDEMAND_HOST_CHECK_STATS, current_time);
				update_check_stats(ACTIVE_CACHED_HOST_CHECK_STATS, current_time);
			} else if (state_change || temp_service->state_type == SOFT_STATE) {
				schedule_next_host_check(temp_host, 0, CHECK_OPTION_DEPENDENCY_CHECK);
			}
		}

		/* else the host is either down or unreachable, so recheck it if necessary */
		else {

			log_debug_info(DEBUGL_CHECKS, 1, "Host is currently %s.\n", host_state_name(temp_host->current_state));

			if (execute_host_checks && (state_change || temp_service->state_type == SOFT_STATE)) {
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
		if (route_result != STATE_UP) {

			log_debug_info(DEBUGL_CHECKS, 2, "Host is not UP, so we mark state changes if appropriate\n");

			/* "fake" a hard state change for the service - well, its not really fake, but it didn't get caught earlier... */
			if (temp_service->last_hard_state != temp_service->current_state) {
				hard_state_change = TRUE;
				nm_log(NSLOG_INFO_MESSAGE, "SERVICE INFO: %s;%s; Service switch to hard down state due to host down.\n", temp_service->host_name, temp_service->description);
			}

			/* update last state change times */
			if (state_change == TRUE || hard_state_change == TRUE)
				temp_service->last_state_change = temp_service->last_check;
			if (hard_state_change == TRUE) {
				temp_service->state_type = HARD_STATE;
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
			if (route_result != STATE_UP) {

				log_debug_info(DEBUGL_CHECKS, 1, "Host isn't UP, so we won't retry the service check...\n");

				/* log the problem as a hard state if the host just went down */
				if (hard_state_change == TRUE) {
					log_service_event(temp_service);
					alert_recorded = NEBATTR_CHECK_ALERT;

					/* run the service event handler to handle the hard state */
					handle_service_event(temp_service);

					/* save the last hard state */
					temp_service->last_hard_state_change = temp_service->last_check;
					temp_service->last_hard_state = temp_service->current_state;
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

				if ((temp_service->current_state != STATE_OK && temp_service->state_type == SOFT_STATE)) {
					if (temp_service->retry_interval != 0.0) {
						/* respect retry interval even if an earlier check is scheduled */
						schedule_next_service_check(temp_service,
						                            get_service_retry_interval_s(temp_service),
						                            CHECK_OPTION_ALLOW_POSTPONE
						                           );
					}
				}
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

			int num_downtimes_start = 0;
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
			if (hard_state_change == TRUE || state_change == TRUE)
				num_downtimes_start = check_pending_flex_service_downtime(temp_service);

			/* 10/04/07 check to see if the service and/or associate host is flapping */
			/* this should be done before a notification is sent out to ensure the host didn't just start flapping */
			check_for_service_flapping(temp_service, TRUE);
			check_for_host_flapping(temp_host, TRUE, FALSE);
			flapping_check_done = TRUE;

			/* (re)send notifications out about this service problem if the host is up (and was at last check also) and the dependencies were okay...
			 * no need to send the notifications if any downtime will be started on this service*/
			if (num_downtimes_start == 0)
				service_notification(temp_service, NOTIFICATION_NORMAL, NULL, NULL, NOTIFICATION_OPTION_NONE);

			/* run the service event handler if we changed state from the last hard state or if this service is flagged as being volatile */
			if (hard_state_change == TRUE || temp_service->is_volatile == TRUE)
				handle_service_event(temp_service);

			/* save the last hard state */
			temp_service->last_hard_state = temp_service->current_state;

		}


		/* should we obsessive over service checks? */
		if (obsess_over_services == TRUE)
			obsessive_compulsive_service_check_processor(temp_service);
	}

	/* if we're stalking this state type and state was not already logged AND the plugin output changed since last check, log it now.. */
	if (temp_service->state_type == HARD_STATE && state_change == FALSE && !alert_recorded && (g_strcmp0(old_plugin_output, temp_service->plugin_output) || g_strcmp0(old_long_plugin_output, temp_service->long_plugin_output))) {
		if (should_stalk(temp_service)) {
			log_service_event(temp_service);
			alert_recorded = NEBATTR_CHECK_ALERT;
		}
	}

	broker_service_check(
	    NEBTYPE_SERVICECHECK_PROCESSED,
	    NEBFLAG_NONE,
	    alert_recorded | first_recorded_state,
	    temp_service,
	    temp_service->check_type,
	    queued_check_result->start_time,
	    queued_check_result->finish_time,
	    temp_service->check_command,
	    temp_service->latency,
	    temp_service->execution_time,
	    service_check_timeout,
	    queued_check_result->early_timeout,
	    queued_check_result->return_code,
	    NULL,
	    queued_check_result);

	/* set the checked flag */
	temp_service->has_been_checked = TRUE;

	/* make sure there is a next check event scheduled */
	if (temp_service->next_check_event == NULL && temp_service->check_interval != 0.0) {
		schedule_next_service_check(temp_service, get_service_check_interval_s(temp_service), CHECK_OPTION_NONE);
	}

	/* update the current service status log */
	update_service_status(temp_service, FALSE);

	/* check to see if the service and/or associate host is flapping */
	if (flapping_check_done == FALSE) {
		check_for_service_flapping(temp_service, TRUE);
		check_for_host_flapping(temp_host, TRUE, FALSE);
	}

	/* update service performance info */
	update_service_performance_data(temp_service);

	/* free allocated memory */
	nm_free(old_plugin_output);
	nm_free(old_long_plugin_output);

	return OK;
}


/******************************************************************************
 ******************************  EXTRA FEATURES  ******************************
 ******************************************************************************/


/* check for services that never returned from a check... */
static void check_for_orphaned_services_eventhandler(struct nm_event_execution_properties *evprop)
{
	service *temp_service = NULL;
	time_t current_time = 0L;
	time_t expected_time = 0L;

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {

		schedule_event(DEFAULT_ORPHAN_CHECK_INTERVAL, check_for_orphaned_services_eventhandler, evprop->user_data);

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
				temp_service->last_update = current_time;

				/* schedule an immediate check of the service */
				schedule_next_service_check(temp_service, 0, CHECK_OPTION_ORPHAN_CHECK);
			}

		}
	}
}


/* event handler for checking freshness of service results */
static void check_service_result_freshness(struct nm_event_execution_properties *evprop)
{
	service *temp_service = NULL;
	time_t current_time = 0L;

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
		/* get the current time */
		time(&current_time);

		schedule_event(service_freshness_check_interval, check_service_result_freshness, evprop->user_data);


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
				schedule_next_service_check(temp_service, 0, CHECK_OPTION_FORCE_EXECUTION | CHECK_OPTION_FRESHNESS_CHECK);
			}

		}
	}
}


/******************************************************************************
 ****************************  STATUS / IMMUTABLE  ****************************
 ******************************************************************************/


/* checks service dependencies */
int check_service_dependencies(service *svc, int dependency_type)
{
	objectlist *list;
	int state = STATE_OK;
	time_t current_time = 0L;

	log_debug_info(DEBUGL_CHECKS, 0, "Service '%s' on host '%s' check_service_dependencies()\n", svc->description, svc->host_name);

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

		log_debug_info(DEBUGL_CHECKS, 1, "  depending on service '%s' on host '%s' with state: %d / has_been_checked: %d\n", temp_service->description, temp_service->host_name, state, temp_service->has_been_checked);

		/* is the service we depend on in state that fails the dependency tests? */
		if (flag_isset(temp_dependency->failure_options, 1 << state))
			return DEPENDENCIES_FAILED;

		/* check for pending flag */
		if (temp_service->has_been_checked == FALSE && flag_isset(temp_dependency->failure_options, OPT_PENDING))
			return DEPENDENCIES_FAILED;

		/* immediate dependencies ok at this point - check parent dependencies if necessary */
		if (temp_dependency->inherits_parent == TRUE) {
			if (check_service_dependencies(temp_service, dependency_type) != DEPENDENCIES_OK)
				return DEPENDENCIES_FAILED;
		}
	}

	return DEPENDENCIES_OK;
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
			freshness_threshold = get_service_check_interval_s(temp_service) + temp_service->latency + additional_freshness_latency;
		else
			freshness_threshold =  get_service_retry_interval_s(temp_service) + temp_service->latency + additional_freshness_latency;
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
