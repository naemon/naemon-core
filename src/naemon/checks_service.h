#ifndef CHECKS_SERVICE_H_
#define CHECKS_SERVICE_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects_service.h"

NAGIOS_BEGIN_DECL

/* initialize service check subsystem */
void checks_init_services(void);

/* Schedule next service check */
void schedule_next_service_check(service *svc, time_t delay, int options);

/* Scheduling, reschedule service to be checked, DEPRECATED */
void schedule_service_check(service *, time_t, int);

/* Result handling, Update a service given a check result */
int handle_async_service_check_result(service *, check_result *);

/* Immutable, check if service is reachable */
int check_service_dependencies(service *, int);

NAGIOS_END_DECL

#endif
