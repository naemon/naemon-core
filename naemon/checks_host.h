#ifndef CHECKS_HOST_H_
#define CHECKS_HOST_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"

NAGIOS_BEGIN_DECL

/* initialize host check subsystem */
void checks_init_hosts(void);

/* Scheduling, reschedule host to be checked */
void schedule_next_host_check(host *hst, time_t delay, int options);
void schedule_host_check(host *hst, time_t check_time, int options); /* DEPRECATED */

/* Result handling, Update a host given a check result */
int handle_async_host_check_result(host *temp_host, check_result *queued_check_result);

/* Immutable, check if host is reachable */
int check_host_dependencies(host *hst, int dependency_type);

NAGIOS_END_DECL

#endif
