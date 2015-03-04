#ifndef _PERFDATA_H
#define _PERFDATA_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects_host.h"
#include "objects_service.h"

NAGIOS_BEGIN_DECL

extern int     perfdata_timeout;
extern char    *host_perfdata_command;
extern char    *service_perfdata_command;
extern char    *host_perfdata_file_template;
extern char    *service_perfdata_file_template;
extern char    *host_perfdata_file;
extern char    *service_perfdata_file;
extern int     host_perfdata_file_append;
extern int     service_perfdata_file_append;
extern int     host_perfdata_file_pipe;
extern int     service_perfdata_file_pipe;
extern unsigned long host_perfdata_file_processing_interval;
extern unsigned long service_perfdata_file_processing_interval;
extern char    *host_perfdata_file_processing_command;
extern char    *service_perfdata_file_processing_command;
extern int     host_perfdata_process_empty_results;
extern int     service_perfdata_process_empty_results;

int initialize_performance_data(const char *);    /* initializes performance data */
int cleanup_performance_data(void);               /* cleans up performance data */

int update_host_performance_data(host *);         /* updates host performance data */
int update_service_performance_data(service *);   /* updates service performance data */

NAGIOS_END_DECL
#endif
