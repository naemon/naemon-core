#ifndef CHECKS_HOST_H_
#define CHECKS_HOST_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"

NAGIOS_BEGIN_DECL


void checks_init_hosts(void);

void check_for_orphaned_hosts(void); /* checks for orphaned hosts */
int check_host_dependencies(host *, int); /* checks host dependencies */
int is_host_result_fresh(host *, time_t, int); /* determines if a host's check results are fresh */
int check_host_check_viability(host *, int, int *, time_t *);
int adjust_host_check_attempt(host *, int);
int determine_host_reachability(host *);
int run_scheduled_host_check(host *, int, double);
int run_async_host_check(host *, int, double, int, int, int *, time_t *);
int handle_async_host_check_result(host *, check_result *);
int handle_host_state(host *, int *); /* top level host state handler */
void schedule_host_check(host *, time_t, int); /* schedules an immediate or delayed host check */


NAGIOS_END_DECL

#endif
