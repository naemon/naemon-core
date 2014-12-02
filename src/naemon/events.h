#ifndef _EVENTS_H
#define _EVENTS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"


/******************* EVENT TYPES **********************/

#define EVENT_SERVICE_CHECK		0	/* active service check */
#define EVENT_COMMAND_CHECK		1	/* external command check */
#define EVENT_LOG_ROTATION		2	/* log file rotation */
#define EVENT_PROGRAM_SHUTDOWN		3	/* program shutdown */
#define EVENT_PROGRAM_RESTART		4	/* program restart */
#define EVENT_CHECK_REAPER              5       /* reaps results from host and service checks */
#define EVENT_ORPHAN_CHECK		6	/* checks for orphaned hosts and services */
#define EVENT_RETENTION_SAVE		7	/* save (dump) retention data */
#define EVENT_STATUS_SAVE		8	/* save (dump) status data */
#define EVENT_SCHEDULED_DOWNTIME	9	/* scheduled host or service downtime */
#define EVENT_SFRESHNESS_CHECK          10      /* checks service result "freshness" */
#define EVENT_EXPIRE_DOWNTIME		11      /* checks for (and removes) expired scheduled downtime */
#define EVENT_HOST_CHECK                12      /* active host check */
#define EVENT_HFRESHNESS_CHECK          13      /* checks host result "freshness" */
#define EVENT_EXPIRE_COMMENT            15      /* removes expired comments */
#define EVENT_CHECK_PROGRAM_UPDATE      16      /* checks for new version of Nagios */
#define EVENT_SLEEP                     98      /* asynchronous sleep event that occurs when event queues are empty */
#define EVENT_USER_FUNCTION             99      /* USER-defined function (modules) */

/*
 * VERSIONFIX: Make EVENT_SLEEP and EVENT_USER_FUNCTION appear
 * linearly in order.
 */

#define EVENT_TYPE_STR(type)	( \
	type == EVENT_SERVICE_CHECK ? "SERVICE_CHECK" : \
	type == EVENT_COMMAND_CHECK ? "COMMAND_CHECK" : \
	type == EVENT_LOG_ROTATION ? "LOG_ROTATION" : \
	type == EVENT_PROGRAM_SHUTDOWN ? "PROGRAM_SHUTDOWN" : \
	type == EVENT_PROGRAM_RESTART ? "PROGRAM_RESTART" : \
	type == EVENT_CHECK_REAPER ? "CHECK_REAPER" : \
	type == EVENT_ORPHAN_CHECK ? "ORPHAN_CHECK" : \
	type == EVENT_RETENTION_SAVE ? "RETENTION_SAVE" : \
	type == EVENT_STATUS_SAVE ? "STATUS_SAVE" : \
	type == EVENT_SCHEDULED_DOWNTIME ? "SCHEDULED_DOWNTIME" : \
	type == EVENT_SFRESHNESS_CHECK ? "SFRESHNESS_CHECK" : \
	type == EVENT_EXPIRE_DOWNTIME ? "EXPIRE_DOWNTIME" : \
	type == EVENT_HOST_CHECK ? "HOST_CHECK" : \
	type == EVENT_HFRESHNESS_CHECK ? "HFRESHNESS_CHECK" : \
	type == EVENT_EXPIRE_COMMENT ? "EXPIRE_COMMENT" : \
	type == EVENT_CHECK_PROGRAM_UPDATE ? "CHECK_PROGRAM_UPDATE" : \
	type == EVENT_SLEEP ? "SLEEP" : \
	type == EVENT_USER_FUNCTION ? "USER_FUNCTION" : \
	"UNKNOWN" \
)

NAGIOS_BEGIN_DECL

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

NAGIOS_END_DECL

#endif
