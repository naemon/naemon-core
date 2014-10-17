#ifndef _EVENTS_H
#define _EVENTS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/squeue.h"


/******************* EVENT TYPES **********************/

NAGIOS_BEGIN_DECL

/* Set if execution of the callback is done normally because of timed event */
#define EVENT_EXEC_FLAG_TIMED	1

/* TIMED_EVENT structure */
struct timed_event;
typedef struct timed_event timed_event;

struct timed_event_properties {
	void *user_data;
	timed_event *event;
	double latency;
	int flags;
};

typedef void (*event_callback)(struct timed_event_properties *);

/**
 * Schedule a timed event. At the given time, the callback is executed
 */
timed_event *schedule_event(time_t delay, event_callback callback, void *user_data);
void destroy_event(timed_event *event);

/* Main function */
void init_event_queue(void); /* creates the queue nagios_squeue */
void event_execution_loop(void); /* main monitoring/event handler loop */
void destroy_event_queue(void); /* destroys the queue nagios_squeue */

NAGIOS_END_DECL

#endif
