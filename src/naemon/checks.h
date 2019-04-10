#ifndef _CHECKS_H
#define _CHECKS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/lnae-utils.h"
#include <stdio.h>
#include <sys/resource.h>

/************ SERVICE DEPENDENCY VALUES ***************/

#define DEPENDENCIES_OK			0
#define DEPENDENCIES_FAILED		1

/***************** OBJECT CHECK TYPES *****************/
#define SERVICE_CHECK                   0
#define HOST_CHECK                      1

/* useful for hosts and services to determine time 'til next check */
#define normal_check_window(o) ((time_t)(o->check_interval * interval_length))
#define retry_check_window(o) ((time_t)(o->retry_interval * interval_length))
/*
 * NOTE: This macro exists for convenience reasons, to avoid having one
 * function for hosts and services each. As such, it's makes a bunch of assumptions, namely;
 * - the passed object has a state_type, current_state, retry_interval and a
 *   check_interval field
 * - the value of STATE_OK (for services) is equal to that of STATE_UP (for hosts)
 **/
#define check_window(o) \
	((o->current_state != STATE_OK && o->state_type == SOFT_STATE) ? \
		retry_check_window(o) : \
		normal_check_window(o))

NAGIOS_BEGIN_DECL

/*
 * *name can be "Nagios Core", "Merlin", "mod_gearman" or "DNX", fe.
 * source_name gets passed the 'source' pointer from check_result
 * and must return a non-free()'able string useful for printing what
 * we need to determine exactly where the check was received from,
 * such as "mod_gearman worker@10.11.12.13", or "Nagios Core command
 * file worker" (for passive checks submitted locally), which will be
 * stashed with hosts and services and used as the "CHECKSOURCE" macro.
 */
struct check_engine {
	char *name;         /* "Nagios Core", "Merlin", "Mod Gearman" fe */
	const char *(*source_name)(void *);
	void (*clean_result)(void *);
};

typedef struct check_result {
	int object_check_type;                          /* is this a service or a host check? */
	char *host_name;                                /* host name */
	char *service_description;                      /* service description */
	int check_type;					/* was this an active or passive service check? */
	int check_options;
	int scheduled_check;                            /* was this a scheduled or an on-demand check? */
	char *output_file;                              /* what file is the output stored in? */
	FILE *output_file_fp;
	double latency;
	struct timeval start_time;			/* time the service check was initiated */
	struct timeval finish_time;			/* time the service check was completed */
	int early_timeout;                              /* did the service check timeout? */
	int exited_ok;					/* did the plugin check return okay? */
	int return_code;				/* plugin return code */
	char *output;	                                /* plugin output */
	struct rusage rusage;			/* resource usage by this check */
	struct check_engine *engine;	/* where did we get this check from? */
	void *source;					/* engine handles this */
} check_result;

struct check_output {
	char *short_output;
	char *long_output;
	char *perf_data;
};

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
