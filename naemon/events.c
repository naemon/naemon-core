#include "config.h"
#include "common.h"
#include "downtime.h"
#include "comments.h"
#include "statusdata.h"
#include "broker.h"
#include "sretention.h"
#include "workers.h"
#include "lib/squeue.h"
#include "events.h"
#include "utils.h"
#include "checks.h"
#include "notifications.h"
#include "logging.h"
#include "globals.h"
#include "defaults.h"
#include "loadctl.h"
#include "nm_alloc.h"
#include <math.h>
#include <string.h>

/* the event we're currently processing */
static timed_event *current_event;

static unsigned int event_count[EVENT_USER_FUNCTION + 1];

/******************************************************************/
/************ EVENT SCHEDULING/HANDLING FUNCTIONS *****************/
/******************************************************************/

int dump_event_stats(int sd)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(event_count); i++) {
		nsock_printf(sd, "%s=%u;", EVENT_TYPE_STR(i), event_count[i]);
		/*
		 * VERSIONFIX: Make EVENT_SLEEP and EVENT_USER_FUNCTION
		 * appear in linear order in include/nagios.h when we go
		 * from 4.0 -> 4.1, so we can remove this junk.
		 */
		if (i == 16)
			i = 97;
	}
	nsock_printf_nul(sd, "SQUEUE_ENTRIES=%u", squeue_size(nagios_squeue));

	return OK;
}


static void track_events(unsigned int type, int add)
{
	/*
	 * remove_event() calls track_events() with add being -1.
	 * add_event() calls us with add being 1
	 */
	if (type < ARRAY_SIZE(event_count))
		event_count[type] += add;
}


/* initialize the event timing loop before we start monitoring */
void init_timing_loop(void)
{
	host *temp_host = NULL;
	service *temp_service = NULL;
	time_t current_time = 0L;
	struct timeval now;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "init_timing_loop() start\n");

	/* get the time and seed the prng */
	gettimeofday(&now, NULL);
	current_time = now.tv_sec;
	srand((now.tv_sec << 10) ^ now.tv_usec);


	/******** GET BASIC HOST/SERVICE INFO  ********/

	/* get info on service checks to be scheduled */
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		/* maybe we shouldn't schedule this check */
		if (temp_service->check_interval == 0 || !temp_service->checks_enabled) {
			log_debug_info(DEBUGL_EVENTS, 1, "Service '%s' on host '%s' should not be scheduled.\n", temp_service->description, temp_service->host_name);
			temp_service->should_be_scheduled = FALSE;
			continue;
		}
	}

	/* get info on host checks to be scheduled */
	for (temp_host = host_list; temp_host; temp_host = temp_host->next) {
		/* host has no check interval */
		if (temp_host->check_interval == 0 || !temp_host->checks_enabled) {
			log_debug_info(DEBUGL_EVENTS, 1, "Host '%s' should not be scheduled.\n", temp_host->name);
			temp_host->should_be_scheduled = FALSE;
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
		temp_service->next_check_event = schedule_new_event(EVENT_SERVICE_CHECK, FALSE, temp_service->next_check, FALSE, 0, NULL, TRUE, (void *)temp_service, NULL, temp_service->check_options);
	}


	/******** SCHEDULE HOST CHECKS  ********/

	log_debug_info(DEBUGL_EVENTS, 2, "Scheduling host checks...");

	/* determine check times for host checks */
	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

		log_debug_info(DEBUGL_EVENTS, 2, "Host '%s'\n", temp_host->name);

		/* skip hosts that shouldn't be scheduled */
		if (temp_host->should_be_scheduled == FALSE) {
			continue;
		}

		temp_host->next_check = current_time + ranged_urand(0, check_window(temp_host));

		log_debug_info(DEBUGL_EVENTS, 2, "Check Time: %lu --> %s", (unsigned long)temp_host->next_check, ctime(&temp_host->next_check));
	}

	/* add scheduled host checks to event queue */
	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

		/* update status of all hosts (scheduled or not) */
		update_host_status(temp_host, FALSE);

		/* skip most hosts that shouldn't be scheduled */
		if (temp_host->should_be_scheduled == FALSE) {

			/* passive checks are an exception if a forced check was scheduled before Nagios was restarted */
			if (!(temp_host->checks_enabled == FALSE && temp_host->next_check != (time_t)0L && (temp_host->check_options & CHECK_OPTION_FORCE_EXECUTION)))
				continue;
		}

		/* schedule a new host check event */
		temp_host->next_check_event = schedule_new_event(EVENT_HOST_CHECK, FALSE, temp_host->next_check, FALSE, 0, NULL, TRUE, (void *)temp_host, NULL, temp_host->check_options);
	}

	/******** SCHEDULE MISC EVENTS ********/

	/* add a check result reaper event */
	schedule_new_event(EVENT_CHECK_REAPER, TRUE, current_time + check_reaper_interval, TRUE, check_reaper_interval, NULL, TRUE, NULL, NULL, 0);

	/* add an orphaned check event */
	if (check_orphaned_services == TRUE || check_orphaned_hosts == TRUE)
		schedule_new_event(EVENT_ORPHAN_CHECK, TRUE, current_time + DEFAULT_ORPHAN_CHECK_INTERVAL, TRUE, DEFAULT_ORPHAN_CHECK_INTERVAL, NULL, TRUE, NULL, NULL, 0);

	/* add a service result "freshness" check event */
	if (check_service_freshness == TRUE)
		schedule_new_event(EVENT_SFRESHNESS_CHECK, TRUE, current_time + service_freshness_check_interval, TRUE, service_freshness_check_interval, NULL, TRUE, NULL, NULL, 0);

	/* add a host result "freshness" check event */
	if (check_host_freshness == TRUE)
		schedule_new_event(EVENT_HFRESHNESS_CHECK, TRUE, current_time + host_freshness_check_interval, TRUE, host_freshness_check_interval, NULL, TRUE, NULL, NULL, 0);

	/* add a status save event */
	schedule_new_event(EVENT_STATUS_SAVE, TRUE, current_time + status_update_interval, TRUE, status_update_interval, NULL, TRUE, NULL, NULL, 0);

	/* add a retention data save event if needed */
	if (retain_state_information == TRUE && retention_update_interval > 0)
		schedule_new_event(EVENT_RETENTION_SAVE, TRUE, current_time + (retention_update_interval * 60), TRUE, (retention_update_interval * 60), NULL, TRUE, NULL, NULL, 0);

	log_debug_info(DEBUGL_FUNCTIONS, 0, "init_timing_loop() end\n");

	return;
}


/*
 * Create the event queue
 * We oversize it somewhat to avoid unnecessary growing
 */
int init_event_queue(void)
{
	unsigned int size;

	size = num_objects.hosts + num_objects.services;
	if (size < 4096)
		size = 4096;

	nagios_squeue = squeue_create(size);
	return 0;
}


/* schedule a new timed event */
timed_event *schedule_new_event(int event_type, int high_priority, time_t run_time, int recurring, unsigned long event_interval, void *timing_func, int compensate_for_time_change, void *event_data, void *event_args, int event_options)
{
	timed_event *new_event;
	char run_time_string[MAX_DATETIME_LENGTH] = "";

	log_debug_info(DEBUGL_FUNCTIONS, 0, "schedule_new_event()\n");

	get_datetime_string(&run_time, run_time_string, MAX_DATETIME_LENGTH,
	                    SHORT_DATE_TIME);
	log_debug_info(DEBUGL_EVENTS, 0, "New Event Details:\n");
	log_debug_info(DEBUGL_EVENTS, 0, " Type:                       EVENT_%s\n",
	               EVENT_TYPE_STR(event_type));
	log_debug_info(DEBUGL_EVENTS, 0, " High Priority:              %s\n",
	               (high_priority ? "Yes" : "No"));
	log_debug_info(DEBUGL_EVENTS, 0, " Run Time:                   %s\n",
	               run_time_string);
	log_debug_info(DEBUGL_EVENTS, 0, " Recurring:                  %s\n",
	               (recurring ? "Yes" : "No"));
	log_debug_info(DEBUGL_EVENTS, 0, " Event Interval:             %lu\n",
	               event_interval);
	log_debug_info(DEBUGL_EVENTS, 0, " Compensate for Time Change: %s\n",
	               (compensate_for_time_change ? "Yes" : "No"));
	log_debug_info(DEBUGL_EVENTS, 0, " Event Options:              %d\n",
	               event_options);

	new_event = nm_calloc(1, sizeof(timed_event));
	if (new_event != NULL) {
		new_event->event_type = event_type;
		new_event->event_data = event_data;
		new_event->event_args = event_args;
		new_event->event_options = event_options;
		new_event->run_time = run_time;
		new_event->recurring = recurring;
		new_event->event_interval = event_interval;
		new_event->timing_func = timing_func;
		new_event->compensate_for_time_change = compensate_for_time_change;
		new_event->priority = high_priority;
	} else
		return NULL;

	log_debug_info(DEBUGL_EVENTS, 0, " Event ID:                   %p\n", new_event);

	/* add the event to the event list */
	add_event(nagios_squeue, new_event);

	return new_event;
}


/* reschedule an event in order of execution time */
void reschedule_event(squeue_t *sq, timed_event *event)
{
	time_t current_time = 0L;
	time_t (*timingfunc)(void);

	log_debug_info(DEBUGL_FUNCTIONS, 0, "reschedule_event()\n");

	/* reschedule recurring events... */
	if (event->recurring == TRUE) {

		/* use custom timing function */
		if (event->timing_func != NULL) {
			timingfunc = event->timing_func;
			event->run_time = (*timingfunc)();
		}

		/* normal recurring events */
		else {
			event->run_time = event->run_time + event->event_interval;
			time(&current_time);
			if (event->run_time < current_time)
				event->run_time = current_time;
		}
	}

	/* add the event to the event list */
	add_event(sq, event);

	return;
}


/* add an event to list ordered by execution time */
void add_event(squeue_t *sq, timed_event *event)
{

	log_debug_info(DEBUGL_FUNCTIONS, 0, "add_event()\n");

	if (event->sq_event) {
		logit(NSLOG_RUNTIME_ERROR, TRUE,
		      "Error: Adding %s event that seems to already be scheduled\n",
		      EVENT_TYPE_STR(event->event_type));
		remove_event(sq, event);
	}

	if (event->priority) {
		event->sq_event = squeue_add_usec(sq, event->run_time, event->priority - 1, event);
	} else {
		event->sq_event = squeue_add(sq, event->run_time, event);
	}
	if (!event->sq_event) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Failed to add event to squeue '%p' with prio %u: %s\n",
		      sq, event->priority, strerror(errno));
	}

	if (sq == nagios_squeue)
		track_events(event->event_type, +1);

#ifdef USE_EVENT_BROKER
	else {
		/* send event data to broker */
		broker_timed_event(NEBTYPE_TIMEDEVENT_ADD, NEBFLAG_NONE, NEBATTR_NONE, event, NULL);
	}
#endif

	return;
}


/* remove an event from the queue */
void remove_event(squeue_t *sq, timed_event *event)
{
#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_timed_event(NEBTYPE_TIMEDEVENT_REMOVE, NEBFLAG_NONE, NEBATTR_NONE, event, NULL);
#endif
	if (!event || !event->sq_event)
		return;

	if (sq)
		squeue_remove(sq, event->sq_event);
	else
		logit(NSLOG_RUNTIME_ERROR, TRUE,
		      "Error: remove_event() called for %s event with NULL sq parameter\n",
		      EVENT_TYPE_STR(event->event_type));

	if (sq == nagios_squeue)
		track_events(event->event_type, -1);

	event->sq_event = NULL; /* mark this event as unscheduled */

	/*
	 * if we catch an event from the queue which gets removed when
	 * we go polling for input (as might happen with f.e. downtime
	 * events that we get "cancel" commands for just as they are
	 * about to start or expire), we must make sure we mark the
	 * current event as no longer scheduled, or we'll run into
	 * segfaults and memory corruptions for sure.
	 */
	if (event == current_event) {
		current_event = NULL;
	}
}


static int should_run_event(timed_event *temp_event)
{
	int run_event = TRUE;	/* default action is to execute the event */
	int nudge_seconds = 0;

	/* we only care about jobs that cause processes to run */
	if (temp_event->event_type != EVENT_HOST_CHECK &&
	    temp_event->event_type != EVENT_SERVICE_CHECK) {
		return TRUE;
	}

	/* if we can't spawn any more jobs, don't bother */
	if (!wproc_can_spawn(&loadctl)) {
		wproc_reap(100, 3000);
		return FALSE;
	}

	/* run a few checks before executing a service check... */
	if (temp_event->event_type == EVENT_SERVICE_CHECK) {
		service *temp_service = (service *)temp_event->event_data;

		/* forced checks override normal check logic */
		if ((temp_service->check_options & CHECK_OPTION_FORCE_EXECUTION))
			return TRUE;

		/* don't run a service check if we're already maxed out on the number of parallel service checks...  */
		if (max_parallel_service_checks != 0 && (currently_running_service_checks >= max_parallel_service_checks)) {
			nudge_seconds = ranged_urand(5, 17);
			logit(NSLOG_RUNTIME_WARNING, TRUE, "\tMax concurrent service checks (%d) has been reached.  Nudging %s:%s by %d seconds...\n", max_parallel_service_checks, temp_service->host_name, temp_service->description, nudge_seconds);
			run_event = FALSE;
		}

		/* don't run a service check if active checks are disabled */
		if (execute_service_checks == FALSE) {
			log_debug_info(DEBUGL_EVENTS | DEBUGL_CHECKS, 1, "We're not executing service checks right now, so we'll skip check event for service '%s;%s'.\n", temp_service->host_name, temp_service->description);
			run_event = FALSE;
		}

		/* reschedule the check if we can't run it now */
		if (run_event == FALSE) {
			remove_event(nagios_squeue, temp_event);

			if (nudge_seconds) {
				/* We nudge the next check time when it is due to too many concurrent service checks */
				temp_service->next_check = (time_t)(temp_service->next_check + nudge_seconds);
			} else {
				temp_service->next_check += check_window(temp_service);
			}

			temp_event->run_time = temp_service->next_check;
			reschedule_event(nagios_squeue, temp_event);
			update_service_status(temp_service, FALSE);

			run_event = FALSE;
		}
	}
	/* run a few checks before executing a host check... */
	else if (temp_event->event_type == EVENT_HOST_CHECK) {
		host *temp_host = (host *)temp_event->event_data;

		/* forced checks override normal check logic */
		if ((temp_host->check_options & CHECK_OPTION_FORCE_EXECUTION))
			return TRUE;

		/* don't run a host check if active checks are disabled */
		if (execute_host_checks == FALSE) {
			log_debug_info(DEBUGL_EVENTS | DEBUGL_CHECKS, 1, "We're not executing host checks right now, so we'll skip host check event for host '%s'.\n", temp_host->name);
			run_event = FALSE;
		}

		/* reschedule the host check if we can't run it right now */
		if (run_event == FALSE) {
			remove_event(nagios_squeue, temp_event);
			temp_host->next_check += check_window(temp_host);
			temp_event->run_time = temp_host->next_check;
			reschedule_event(nagios_squeue, temp_event);
			update_host_status(temp_host, FALSE);
			run_event = FALSE;
		}
	}

	return run_event;
}


/* this is the main event handler loop */
int event_execution_loop(void)
{
	timed_event *temp_event, *last_event = NULL;
	time_t last_time = 0L;
	time_t current_time = 0L;
	time_t last_status_update = 0L;
	int poll_time_ms;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "event_execution_loop() start\n");

	time(&last_time);

	while (1) {
		struct timeval now;
		const struct timeval *event_runtime;
		int inputs;

		/* super-priority (hardcoded) events come first */

		/* see if we should exit or restart (a signal was encountered) */
		if (sigshutdown == TRUE || sigrestart == TRUE)
			break;

		/* get the current time */
		time(&current_time);

		if (sigrotate == TRUE) {
			rotate_log_file(current_time);
			update_program_status(FALSE);
		}

		/* hey, wait a second...  we traveled back in time! */
		if (current_time < last_time)
			compensate_for_system_time_change((unsigned long)last_time, (unsigned long)current_time);

		/* else if the time advanced over the specified threshold, try and compensate... */
		else if ((current_time - last_time) >= time_change_threshold)
			compensate_for_system_time_change((unsigned long)last_time, (unsigned long)current_time);

		/* get next scheduled event */
		current_event = temp_event = (timed_event *)squeue_peek(nagios_squeue);

		/* if we don't have any events to handle, exit */
		if (!temp_event) {
			log_debug_info(DEBUGL_EVENTS, 0, "There aren't any events that need to be handled! Exiting...\n");
			break;
		}

		/* keep track of the last time */
		last_time = current_time;

		/* update status information occassionally - NagVis watches the NDOUtils DB to see if Nagios is alive */
		if ((unsigned long)(current_time - last_status_update) > 5) {
			last_status_update = current_time;
			update_program_status(FALSE);
		}

		event_runtime = squeue_event_runtime(temp_event->sq_event);
		if (temp_event != last_event) {
			log_debug_info(DEBUGL_EVENTS, 1, "** Event Check Loop\n");
			log_debug_info(DEBUGL_EVENTS, 1, "Next Event Time: %s", ctime(&temp_event->run_time));
			log_debug_info(DEBUGL_EVENTS, 1, "Current/Max Service Checks: %d/%d (%.3lf%% saturation)\n",
			               currently_running_service_checks, max_parallel_service_checks,
			               ((float)currently_running_service_checks / (float)max_parallel_service_checks) * 100);
		}

		last_event = temp_event;

		gettimeofday(&now, NULL);
		poll_time_ms = tv_delta_msec(&now, event_runtime);
		if (poll_time_ms < 0)
			poll_time_ms = 0;
		else if (poll_time_ms >= 1500)
			poll_time_ms = 1500;

		log_debug_info(DEBUGL_SCHEDULING, 2, "## Polling %dms; sockets=%d; events=%u; iobs=%p\n",
		               poll_time_ms, iobroker_get_num_fds(nagios_iobs),
		               squeue_size(nagios_squeue), nagios_iobs);
		inputs = iobroker_poll(nagios_iobs, poll_time_ms);
		if (inputs < 0 && errno != EINTR) {
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Polling for input on %p failed: %s", nagios_iobs, iobroker_strerror(inputs));
			break;
		}

		log_debug_info(DEBUGL_IPC, 2, "## %d descriptors had input\n", inputs);

		/*
		 * if the event we peaked was removed from the queue from
		 * one of the I/O operations, we must take care not to
		 * try to run at, as we're (almost) sure to access free'd
		 * or invalid memory if we do.
		 */
		if (!current_event) {
			log_debug_info(DEBUGL_EVENTS, 0, "Event was cancelled by iobroker input\n");
			continue;
		}

		gettimeofday(&now, NULL);
		if (tv_delta_msec(&now, event_runtime) >= 0)
			continue;

		/* move on if we shouldn't run this event */
		if (should_run_event(temp_event) == FALSE)
			continue;

		/* handle the event */
		handle_timed_event(temp_event);

		/*
		 * we must remove the entry we've peeked, or
		 * we'll keep getting the same one over and over.
		 * This also maintains sync with broker modules.
		 */
		remove_event(nagios_squeue, temp_event);

		/* reschedule the event if necessary */
		if (temp_event->recurring == TRUE)
			reschedule_event(nagios_squeue, temp_event);

		/* else free memory associated with the event */
		else
			my_free(temp_event);
	}

	log_debug_info(DEBUGL_FUNCTIONS, 0, "event_execution_loop() end\n");

	return OK;
}


/* handles a timed event */
int handle_timed_event(timed_event *event)
{
	host *temp_host = NULL;
	service *temp_service = NULL;
	void (*userfunc)(void *);
	struct timeval tv;
	const struct timeval *event_runtime;
	double latency;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "handle_timed_event() start\n");

#ifdef USE_EVENT_BROKER
	/* send event data to broker */
	broker_timed_event(NEBTYPE_TIMEDEVENT_EXECUTE, NEBFLAG_NONE, NEBATTR_NONE, event, NULL);
#endif

	log_debug_info(DEBUGL_EVENTS, 0, "** Timed Event ** Type: EVENT_%s, Run Time: %s", EVENT_TYPE_STR(event->event_type), ctime(&event->run_time));

	/* get event latency */
	gettimeofday(&tv, NULL);
	event_runtime = squeue_event_runtime(event->sq_event);
	latency = (double)(tv_delta_f(event_runtime, &tv));
	if (latency < 0.0) /* events may run up to 0.1 seconds early */
		latency = 0.0;

	/* how should we handle the event? */
	switch (event->event_type) {

	case EVENT_SERVICE_CHECK:

		temp_service = (service *)event->event_data;

		log_debug_info(DEBUGL_EVENTS, 0, "** Service Check Event ==> Host: '%s', Service: '%s', Options: %d, Latency: %f sec\n", temp_service->host_name, temp_service->description, event->event_options, latency);

		/* run the service check */
		run_scheduled_service_check(temp_service, event->event_options, latency);
		break;

	case EVENT_HOST_CHECK:

		temp_host = (host *)event->event_data;

		log_debug_info(DEBUGL_EVENTS, 0, "** Host Check Event ==> Host: '%s', Options: %d, Latency: %f sec\n", temp_host->name, event->event_options, latency);

		/* run the host check */
		run_scheduled_host_check(temp_host, event->event_options, latency);
		break;

	case EVENT_PROGRAM_SHUTDOWN:

		log_debug_info(DEBUGL_EVENTS, 0, "** Program Shutdown Event. Latency: %.3fs\n", latency);

		/* set the shutdown flag */
		sigshutdown = TRUE;

		/* log the shutdown */
		logit(NSLOG_PROCESS_INFO, TRUE, "PROGRAM_SHUTDOWN event encountered, shutting down...\n");
		break;

	case EVENT_PROGRAM_RESTART:

		log_debug_info(DEBUGL_EVENTS, 0, "** Program Restart Event. Latency: %.3fs\n", latency);

		/* set the restart flag */
		sigrestart = TRUE;

		/* log the restart */
		logit(NSLOG_PROCESS_INFO, TRUE, "PROGRAM_RESTART event encountered, restarting...\n");
		break;

	case EVENT_CHECK_REAPER:

		log_debug_info(DEBUGL_EVENTS, 0, "** Check Result Reaper. Latency: %.3fs\n", latency);

		/* reap host and service check results */
		reap_check_results();
		break;

	case EVENT_ORPHAN_CHECK:

		log_debug_info(DEBUGL_EVENTS, 0, "** Orphaned Host and Service Check Event. Latency: %.3fs\n", latency);

		/* check for orphaned hosts and services */
		if (check_orphaned_hosts == TRUE)
			check_for_orphaned_hosts();
		if (check_orphaned_services == TRUE)
			check_for_orphaned_services();
		break;

	case EVENT_RETENTION_SAVE:

		log_debug_info(DEBUGL_EVENTS, 0, "** Retention Data Save Event. Latency: %.3fs\n", latency);

		/* save state retention data */
		save_state_information(TRUE);
		break;

	case EVENT_STATUS_SAVE:

		log_debug_info(DEBUGL_EVENTS, 0, "** Status Data Save Event. Latency: %.3fs\n", latency);

		/* save all status data (program, host, and service) */
		update_all_status_data();
		break;

	case EVENT_SCHEDULED_DOWNTIME:

		log_debug_info(DEBUGL_EVENTS, 0, "** Scheduled Downtime Event. Latency: %.3fs\n", latency);

		/* process scheduled downtime info */
		if (event->event_data) {
			handle_scheduled_downtime_by_id(*(unsigned long *)event->event_data);
			free(event->event_data);
			event->event_data = NULL;
		}
		break;

	case EVENT_SFRESHNESS_CHECK:

		log_debug_info(DEBUGL_EVENTS, 0, "** Service Result Freshness Check Event. Latency: %.3fs\n", latency);

		/* check service result freshness */
		check_service_result_freshness();
		break;

	case EVENT_HFRESHNESS_CHECK:

		log_debug_info(DEBUGL_EVENTS, 0, "** Host Result Freshness Check Event. Latency: %.3fs\n", latency);

		/* check host result freshness */
		check_host_result_freshness();
		break;

	case EVENT_EXPIRE_DOWNTIME:

		log_debug_info(DEBUGL_EVENTS, 0, "** Expire Downtime Event. Latency: %.3fs\n", latency);

		/* check for expired scheduled downtime entries */
		check_for_expired_downtime();
		break;

	case EVENT_EXPIRE_COMMENT:

		log_debug_info(DEBUGL_EVENTS, 0, "** Expire Comment Event. Latency: %.3fs\n", latency);

		/* check for expired comment */
		check_for_expired_comment((unsigned long)event->event_data);
		break;

	case EVENT_CHECK_PROGRAM_UPDATE:
		/* this doesn't do anything anymore */
		break;

	case EVENT_USER_FUNCTION:

		log_debug_info(DEBUGL_EVENTS, 0, "** User Function Event. Latency: %.3fs\n", latency);

		/* run a user-defined function */
		if (event->event_data != NULL) {
			userfunc = event->event_data;
			(*userfunc)(event->event_args);
		}
		break;

	default:

		break;
	}

	log_debug_info(DEBUGL_FUNCTIONS, 0, "handle_timed_event() end\n");

	return OK;
}

static void adjust_squeue_for_time_change(squeue_t **q, int delta)
{
	timed_event *event;
	squeue_t *sq_new;

	/*
	 * this is pretty inefficient in terms of free() + malloc(),
	 * but it should be pretty rare that we have to adjust times
	 * so we go with the well-tested codepath.
	 */
	sq_new = squeue_create(squeue_size(*q));
	while ((event = squeue_pop(*q))) {
		if (event->compensate_for_time_change == TRUE) {
			if (event->timing_func) {
				time_t (*timingfunc)(void);
				timingfunc = event->timing_func;
				event->run_time = timingfunc();
			} else {
				event->run_time += delta;
			}
		}
		if (event->priority) {
			event->sq_event = squeue_add_usec(sq_new, event->run_time, event->priority - 1, event);
		} else {
			event->sq_event = squeue_add(sq_new, event->run_time, event);
		}
	}
	squeue_destroy(*q, 0);
	*q = sq_new;
}


/* attempts to compensate for a change in the system time */
void compensate_for_system_time_change(unsigned long last_time, unsigned long current_time)
{
	unsigned long time_difference = 0L;
	service *temp_service = NULL;
	host *temp_host = NULL;
	int days = 0;
	int hours = 0;
	int minutes = 0;
	int seconds = 0;
	int delta = 0;


	log_debug_info(DEBUGL_FUNCTIONS, 0, "compensate_for_system_time_change() start\n");

	/*
	 * if current_time < last_time, delta will be negative so we can
	 * still use addition to all effected timestamps
	 */
	delta = current_time - last_time;

	/* we moved back in time... */
	if (last_time > current_time) {
		time_difference = last_time - current_time;
		get_time_breakdown(time_difference, &days, &hours, &minutes, &seconds);
		log_debug_info(DEBUGL_EVENTS, 0, "Detected a backwards time change of %dd %dh %dm %ds.\n", days, hours, minutes, seconds);
	}

	/* we moved into the future... */
	else {
		time_difference = current_time - last_time;
		get_time_breakdown(time_difference, &days, &hours, &minutes, &seconds);
		log_debug_info(DEBUGL_EVENTS, 0, "Detected a forwards time change of %dd %dh %dm %ds.\n", days, hours, minutes, seconds);
	}

	/* log the time change */
	logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_WARNING, TRUE, "Warning: A system time change of %d seconds (%dd %dh %dm %ds %s in time) has been detected.  Compensating...\n",
	      delta, days, hours, minutes, seconds,
	      (last_time > current_time) ? "backwards" : "forwards");

	adjust_squeue_for_time_change(&nagios_squeue, delta);

	/* adjust service timestamps */
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_service->last_notification);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_service->last_check);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_service->next_check);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_service->last_state_change);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_service->last_hard_state_change);

		/* recalculate next re-notification time */
		temp_service->next_notification = get_next_service_notification_time(temp_service, temp_service->last_notification);

		/* update the status data */
		update_service_status(temp_service, FALSE);
	}

	/* adjust host timestamps */
	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_host->last_notification);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_host->last_check);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_host->next_check);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_host->last_state_change);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_host->last_hard_state_change);
		adjust_timestamp_for_time_change(last_time, current_time, time_difference, &temp_host->last_state_history_update);

		/* recalculate next re-notification time */
		temp_host->next_notification = get_next_host_notification_time(temp_host, temp_host->last_notification);

		/* update the status data */
		update_host_status(temp_host, FALSE);
	}

	/* adjust program timestamps */
	adjust_timestamp_for_time_change(last_time, current_time, time_difference, &program_start);
	adjust_timestamp_for_time_change(last_time, current_time, time_difference, &event_start);

	/* update the status data */
	update_program_status(FALSE);

	return;
}


/* adjusts a timestamp variable in accordance with a system time change */
void adjust_timestamp_for_time_change(time_t last_time, time_t current_time, unsigned long time_difference, time_t *ts)
{

	log_debug_info(DEBUGL_FUNCTIONS, 0, "adjust_timestamp_for_time_change()\n");

	/* we shouldn't do anything with epoch values */
	if (*ts == (time_t)0)
		return;

	/* we moved back in time... */
	if (last_time > current_time) {

		/* we can't precede the UNIX epoch */
		if (time_difference > (unsigned long)*ts)
			*ts = (time_t)0;
		else
			*ts = (time_t)(*ts - time_difference);
	}

	/* we moved into the future... */
	else
		*ts = (time_t)(*ts + time_difference);

	return;
}
