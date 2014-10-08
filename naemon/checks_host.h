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
int handle_async_host_check_result(host *, check_result *);
void schedule_host_check(host *, time_t, int); /* schedules an immediate or delayed host check */

NAGIOS_END_DECL

#endif
