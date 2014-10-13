#ifndef _EVENTS_H
#define _EVENTS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/squeue.h"


/******************* EVENT TYPES **********************/

/* checks.c */
#define EVENT_SERVICE_CHECK		0	/* active service check */
#define EVENT_HOST_CHECK                12      /* active host check */

/* Generic */
#define EVENT_USER_FUNCTION             99      /* USER-defined function (modules) */

#define EVENT_TYPE_STR(type)	( \
	type == EVENT_SERVICE_CHECK ? "SERVICE_CHECK" : \
	type == EVENT_HOST_CHECK ? "HOST_CHECK" : \
	type == EVENT_USER_FUNCTION ? "USER_FUNCTION" : \
	"UNKNOWN" \
)

NAGIOS_BEGIN_DECL

/* TIMED_EVENT structure */
typedef struct timed_event {
	int event_type;
	time_t run_time;
	int recurring;
	unsigned long event_interval;
	int compensate_for_time_change;
	void *timing_func;
	void *event_data;
	void *event_args;
	int event_options;
	unsigned int priority; /* 0 is auto, 1 is highest. n+1 < n */
	struct squeue_event *sq_event;
} timed_event;

int init_event_queue(void); /* creates the queue nagios_squeue */

/**
 * Schedule a timed event. At the given time, the callback is executed
 */
timed_event *schedule_event(time_t time_left, void (*callback)(void *), void *args);

timed_event *schedule_new_event(int, int, time_t, int, unsigned long, void *, int, void *, void *, int);	/* schedules a new timed event */

/* Only used internally */
void reschedule_event(squeue_t *sq, timed_event *event);   		/* reschedules an event */

int handle_timed_event(timed_event *);		     		/* top level handler for timed events */
void compensate_for_system_time_change(unsigned long, unsigned long);	/* attempts to compensate for a change in the system time */

/* Lowlevel interface, deprecated */
void add_event(squeue_t *sq, timed_event *event);     		/* adds an event to the execution queue */
void remove_event(squeue_t *sq, timed_event *event);     		/* remove an event from the execution queue */

/* Main function */
int event_execution_loop(void);                      		/* main monitoring/event handler loop */

void adjust_timestamp_for_time_change(time_t, time_t, unsigned long, time_t *); /* adjusts a timestamp variable for a system time change */

NAGIOS_END_DECL

#endif
