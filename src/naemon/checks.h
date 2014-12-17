#ifndef _CHECKS_H
#define _CHECKS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"

/************ SERVICE DEPENDENCY VALUES ***************/

#define DEPENDENCIES_OK			0
#define DEPENDENCIES_FAILED		1

/***************** OBJECT CHECK TYPES *****************/
#define SERVICE_CHECK                   0
#define HOST_CHECK                      1

/* useful for hosts and services to determine time 'til next check */
#define normal_check_window(o) ((time_t)(o->check_interval * interval_length))
#define retry_check_window(o) ((time_t)(o->retry_interval * interval_length))
#define next_check_time(o) _next_check_time(o->last_check, check_window(o))
#define check_window(o) \
	((!o->current_state && o->state_type == SOFT_STATE) ? \
		retry_check_window(o) : \
		normal_check_window(o))

NAGIOS_BEGIN_DECL

static inline int _next_check_time(time_t last_check, time_t window)
{
	time_t now = time(NULL);

	/* if *_interval is 0, we mustn't schedule things immediately */
	if (!window)
		window = interval_length;

	/* last_check may be 0 or very far back if we've been shut down */
	if (last_check < (now - window))
		last_check = now;

	return last_check + window;
}

int parse_check_output(char *, char **, char **, char **, int, int);
struct check_output *parse_output(const char *, struct check_output *);
int check_service_dependencies(service *, int);          	/* checks service dependencies */
int check_host_dependencies(host *, int);                	/* checks host dependencies */
void check_for_orphaned_services(void);				/* checks for orphaned services */
void check_for_orphaned_hosts(void);				/* checks for orphaned hosts */
void check_service_result_freshness(void);              	/* checks the "freshness" of service check results */
int is_service_result_fresh(service *, time_t, int);            /* determines if a service's check results are fresh */
void check_host_result_freshness(void);                 	/* checks the "freshness" of host check results */
int is_host_result_fresh(host *, time_t, int);                  /* determines if a host's check results are fresh */

int check_host_check_viability(host *, int, int *, time_t *);
int adjust_host_check_attempt(host *, int);
int determine_host_reachability(host *);
int run_scheduled_host_check(host *, int, double);
int run_async_host_check(host *, int, double, int, int, int *, time_t *);
int handle_async_host_check_result(host *, check_result *);

int check_service_check_viability(service *, int, int *, time_t *);
int run_scheduled_service_check(service *, int, double);
int run_async_service_check(service *, int, double, int, int, int *, time_t *);
int handle_async_service_check_result(service *, check_result *);

int handle_host_state(host *, int *);               			/* top level host state handler */

int reap_check_results(void);

void schedule_service_check(service *, time_t, int);	/* schedules an immediate or delayed service check */
void schedule_host_check(host *, time_t, int);		/* schedules an immediate or delayed host check */

/* GONE?!? */
int perform_on_demand_host_check(host *, int *, int, int, unsigned long);
int execute_sync_host_check(host *);

NAGIOS_END_DECL

#endif
