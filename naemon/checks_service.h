#ifndef CHECKS_SERVICE_H_
#define CHECKS_SERVICE_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"

NAGIOS_BEGIN_DECL

void checks_init_services(void);
int check_service_dependencies(service *, int); /* checks service dependencies */
int handle_async_service_check_result(service *, check_result *);
void schedule_service_check(service *, time_t, int); /* schedules an immediate or delayed service check */

NAGIOS_END_DECL

#endif
