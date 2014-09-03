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

int xpddefault_run_service_performance_data_command(nagios_macros *mac, service *);
int xpddefault_run_host_performance_data_command(nagios_macros *mac, host *);

int xpddefault_update_service_performance_data_file(nagios_macros *mac, service *);
int xpddefault_update_host_performance_data_file(nagios_macros *mac, host *);

int xpddefault_preprocess_file_templates(char *);

int xpddefault_open_host_perfdata_file(void);
int xpddefault_open_service_perfdata_file(void);
int xpddefault_close_host_perfdata_file(void);
int xpddefault_close_service_perfdata_file(void);

int xpddefault_process_host_perfdata_file(void);
int xpddefault_process_service_perfdata_file(void);

NAGIOS_END_DECL

#endif
