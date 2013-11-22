#ifndef _XSDDEFAULT_H
#define _XSDDEFAULT_H

#ifdef NSCORE
int xsddefault_initialize_status_data(const char *);
int xsddefault_cleanup_status_data(int);
int xsddefault_save_status_data(void);
#endif

#ifdef NSCGI

#define XSDDEFAULT_NO_DATA               0
#define XSDDEFAULT_INFO_DATA             1
#define XSDDEFAULT_PROGRAMSTATUS_DATA    2
#define XSDDEFAULT_HOSTSTATUS_DATA       3
#define XSDDEFAULT_SERVICESTATUS_DATA    4
#define XSDDEFAULT_CONTACTSTATUS_DATA    5
#define XSDDEFAULT_HOSTCOMMENT_DATA      6
#define XSDDEFAULT_SERVICECOMMENT_DATA   7
#define XSDDEFAULT_HOSTDOWNTIME_DATA     8
#define XSDDEFAULT_SERVICEDOWNTIME_DATA  9

int xsddefault_read_status_data(const char *, int);
#endif

#endif
