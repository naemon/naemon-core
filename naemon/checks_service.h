#ifndef CHECKS_SERVICE_H_
#define CHECKS_SERVICE_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"

NAGIOS_BEGIN_DECL

void checks_init_services(void);

void check_for_orphaned_services(void);				/* checks for orphaned services */
int check_service_dependencies(service *, int);          	/* checks service dependencies */
int is_service_result_fresh(service *, time_t, int);            /* determines if a service's check results are fresh */
int check_service_check_viability(service *, int, int *, time_t *);
int run_scheduled_service_check(service *, int, double);
int run_async_service_check(service *, int, double, int, int, int *, time_t *);
int handle_async_service_check_result(service *, check_result *);
void schedule_service_check(service *, time_t, int);	/* schedules an immediate or delayed service check */

NAGIOS_END_DECL

#endif
