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
#define check_window(o) \
	((!o->current_state && o->state_type == SOFT_STATE) ? \
		retry_check_window(o) : \
		normal_check_window(o))

NAGIOS_BEGIN_DECL

/*********************** GENERIC **********************/
void checks_init(void); /* Init check execution, schedule events */

int parse_check_output(char *, char **, char **, char **, int, int);
struct check_output *parse_output(const char *, struct check_output *);

int process_check_result_queue(char *);
int process_check_result_file(char *);
int process_check_result(check_result *);
int delete_check_result_file(char *);
int init_check_result(check_result *);
int free_check_result(check_result *);                  	/* frees memory associated with a host/service check result */

NAGIOS_END_DECL

#endif
