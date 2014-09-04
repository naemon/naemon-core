#ifndef _PERFDATA_H
#define _PERFDATA_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "common.h"
#include "objects.h"

NAGIOS_BEGIN_DECL

int initialize_performance_data(const char *);    /* initializes performance data */
int cleanup_performance_data(void);               /* cleans up performance data */

int update_host_performance_data(host *);         /* updates host performance data */
int update_service_performance_data(service *);   /* updates service performance data */

NAGIOS_END_DECL
#endif
