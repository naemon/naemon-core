#ifndef _XSDDEFAULT_H
#define _XSDDEFAULT_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

NAGIOS_BEGIN_DECL

int xsddefault_initialize_status_data(const char *);
int xsddefault_cleanup_status_data(int);
int xsddefault_save_status_data(void);

NAGIOS_END_DECL

#endif
