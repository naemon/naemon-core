#include "config.h"
#include "common.h"
#include "statusdata.h"
#include "broker.h"
#include "sretention.h"
#include "lib/squeue.h"
#include "events.h"
#include "utils.h"
#include "checks.h"
#include "notifications.h"
#include "logging.h"
#include "globals.h"
#include "defaults.h"
#include "nm_alloc.h"
#include <math.h>
#include <string.h>

/* the event we're currently processing */
static timed_event *current_event;

/******************************************************************/
/************ EVENT SCHEDULING/HANDLING FUNCTIONS *****************/
/******************************************************************/

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


timed_event *schedule_event(time_t time_left, void (*callback)(void *), void *args)
{
	timed_event *new_event = nm_calloc(1, sizeof(timed_event));
	time_t run_time = time_left + time(NULL);

	new_event->event_type = EVENT_USER_FUNCTION;
	new_event->event_data = (void*)callback;
	new_event->event_args = args;
	new_event->event_options = 0;
	new_event->run_time = run_time;
	new_event->recurring = FALSE;
	new_event->event_interval = 0;
	new_event->timing_func = NULL;
	new_event->compensate_for_time_change = 0;
	new_event->priority = 0;

	add_event(nagios_squeue, new_event);

	return new_event;
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
		nm_log(NSLOG_RUNTIME_ERROR,
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
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to add event to squeue '%p' with prio %u: %s\n",
		       sq, event->priority, strerror(errno));
	}

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
		nm_log(NSLOG_RUNTIME_ERROR,
		       "Error: remove_event() called for %s event with NULL sq parameter\n",
		       EVENT_TYPE_STR(event->event_type));

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
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Polling for input on %p failed: %s", nagios_iobs, iobroker_strerror(inputs));
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
		if (tv_delta_msec(&now, event_runtime) > 0)
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
			nm_free(temp_event);
	}

	log_debug_info(DEBUGL_FUNCTIONS, 0, "event_execution_loop() end\n");

	return OK;
}


/* handles a timed event */
int handle_timed_event(timed_event *event)
{
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
	nm_log(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_WARNING, "Warning: A system time change of %d seconds (%dd %dh %dm %ds %s in time) has been detected.  Compensating...\n",
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
