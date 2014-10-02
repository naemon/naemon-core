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
#define next_check_time(o) (o->last_check + check_window(o))
#define check_window(o) \
	((!o->current_state && o->state_type == SOFT_STATE) ? \
		retry_check_window(o) : \
		normal_check_window(o))

NAGIOS_BEGIN_DECL

/*********************** GENERIC **********************/
void checks_init(void); /* Init check execution, schedule events */

int parse_check_output(char *, char **, char **, char **, int, int);
struct check_output *parse_output(const char *, struct check_output *);

/*********************** HOSTS ************************/
int check_host_dependencies(host *, int);                	/* checks host dependencies */
int is_host_result_fresh(host *, time_t, int);                  /* determines if a host's check results are fresh */
int check_host_check_viability(host *, int, int *, time_t *);
int adjust_host_check_attempt(host *, int);
int determine_host_reachability(host *);
int run_scheduled_host_check(host *, int, double);
int run_async_host_check(host *, int, double, int, int, int *, time_t *);
int handle_async_host_check_result(host *, check_result *);
int handle_host_state(host *, int *);               			/* top level host state handler */
void schedule_host_check(host *, time_t, int);		/* schedules an immediate or delayed host check */


/********************** SERVICES **********************/
int check_service_dependencies(service *, int);          	/* checks service dependencies */
int is_service_result_fresh(service *, time_t, int);            /* determines if a service's check results are fresh */
int check_service_check_viability(service *, int, int *, time_t *);
int run_scheduled_service_check(service *, int, double);
int run_async_service_check(service *, int, double, int, int, int *, time_t *);
int handle_async_service_check_result(service *, check_result *);
void schedule_service_check(service *, time_t, int);	/* schedules an immediate or delayed service check */

NAGIOS_END_DECL

#endif
