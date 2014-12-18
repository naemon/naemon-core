#ifndef _XPDDEFAULT_H
#define _XPDDEFAULT_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"

NAGIOS_BEGIN_DECL

int xpddefault_initialize_performance_data(const char *);
int xpddefault_cleanup_performance_data(void);

int xpddefault_update_service_performance_data(service *);
int xpddefault_update_host_performance_data(host *);

NAGIOS_END_DECL

#endif
