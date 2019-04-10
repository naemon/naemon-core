#ifndef _EVENTS_H
#define _EVENTS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/lnae-utils.h"
#include "lib/iobroker.h"

/******************* EVENT TYPES **********************/

NAGIOS_BEGIN_DECL

extern iobroker_set *nagios_iobs;

/* Set if execution of the callback is done normally because of timed event */
enum nm_exec_type {
	EVENT_EXEC_NORMAL, /* Everything was fine, the event is a proper event */
	EVENT_EXEC_ABORTED, /* The event wasn't completed: a timeout for timed events, or a socket problem for input events */
};

enum nm_event_type {
	EVENT_TYPE_TIMED,
};

struct timed_event;
typedef struct timed_event timed_event;

struct nm_event_execution_properties {
	enum nm_exec_type execution_type;
	enum nm_event_type event_type;
	void *user_data;
	union {
		struct {
			timed_event *event;
			double latency;
		} timed; /* only available if event_type is EVENT_EXEC_FLAG_TIMED */
	} attributes;
};

typedef void (*event_callback)(struct nm_event_execution_properties *);

/**
 * Schedule a timed event. At the given time, the callback is executed
 */
timed_event *schedule_event(time_t delay, event_callback callback, void *user_data);
void destroy_event(timed_event *event);

/**
 * Gets the number of milliseconds until the supplied event's scheduled
 * execution time
 * @param ev the \p timed_event
 * @return The number of milliseconds
 */
long get_timed_event_time_left_ms(timed_event *ev);

/* Main function */
void init_event_queue(void); /* creates the queue nagios_squeue */
int event_poll(void); /* main monitoring/event handler loop */
void destroy_event_queue(void); /* destroys the queue nagios_squeue */

NAGIOS_END_DECL

#endif
