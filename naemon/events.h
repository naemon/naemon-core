#ifndef _EVENTS_H
#define _EVENTS_H

#include "objects.h"

int dump_event_stats(int sd);
void init_timing_loop(void);                         		/* setup the initial scheduling queue */
void display_scheduling_info(void);				/* displays service check scheduling information */
int init_event_queue(void); /* creates the queue nagios_squeue */
timed_event *schedule_new_event(int, int, time_t, int, unsigned long, void *, int, void *, void *, int);	/* schedules a new timed event */
void reschedule_event(squeue_t *sq, timed_event *event);   		/* reschedules an event */
void add_event(squeue_t *sq, timed_event *event);     		/* adds an event to the execution queue */
void remove_event(squeue_t *sq, timed_event *event);     		/* remove an event from the execution queue */
int event_execution_loop(void);                      		/* main monitoring/event handler loop */
int handle_timed_event(timed_event *);		     		/* top level handler for timed events */
void adjust_check_scheduling(void);		        	/* auto-adjusts scheduling of host and service checks */
void compensate_for_system_time_change(unsigned long, unsigned long);	/* attempts to compensate for a change in the system time */
void adjust_timestamp_for_time_change(time_t, time_t, unsigned long, time_t *); /* adjusts a timestamp variable for a system time change */

#endif
