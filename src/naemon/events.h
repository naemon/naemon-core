#ifndef _EVENTS_H
#define _EVENTS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/squeue.h"


/******************* EVENT TYPES **********************/

NAGIOS_BEGIN_DECL

typedef void (*event_callback)(void *);

/* TIMED_EVENT structure */
struct timed_event;
typedef struct timed_event timed_event;


/**
 * Schedule a timed event. At the given time, the callback is executed
 */
timed_event *schedule_event(time_t time_left, event_callback callback, void *storage);
void destroy_event(timed_event *event);

/* Main function */
void init_event_queue(void); /* creates the queue nagios_squeue */
void event_execution_loop(void); /* main monitoring/event handler loop */
void destroy_event_queue(void); /* destroys the queue nagios_squeue */

NAGIOS_END_DECL

#endif
