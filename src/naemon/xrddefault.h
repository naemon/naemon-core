#ifndef _XRDDEFAULT_H
#define _XRDDEFAULT_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#define XRDDEFAULT_NO_DATA               0
#define XRDDEFAULT_INFO_DATA             1
#define XRDDEFAULT_PROGRAMSTATUS_DATA    2
#define XRDDEFAULT_HOSTSTATUS_DATA       3
#define XRDDEFAULT_SERVICESTATUS_DATA    4
#define XRDDEFAULT_CONTACTSTATUS_DATA    5
#define XRDDEFAULT_HOSTCOMMENT_DATA      6
#define XRDDEFAULT_SERVICECOMMENT_DATA   7
#define XRDDEFAULT_HOSTDOWNTIME_DATA     8
#define XRDDEFAULT_SERVICEDOWNTIME_DATA  9

NAGIOS_BEGIN_DECL

int xrddefault_initialize_retention_data(void);
int xrddefault_cleanup_retention_data(void);
int xrddefault_save_state_information(void);        /* saves all host and service state information */
int xrddefault_read_state_information(void);        /* reads in initial host and service state information */

NAGIOS_END_DECL
#endif
