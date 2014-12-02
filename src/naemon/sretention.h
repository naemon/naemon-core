#ifndef _SRETENTION_H
#define _SRETENTION_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "common.h"
NAGIOS_BEGIN_DECL

int initialize_retention_data(const char *);
int cleanup_retention_data(void);
int save_state_information(int);                 /* saves all host and state information */
int read_initial_state_information(void);        /* reads in initial host and state information */
int pre_modify_contact_attribute(struct contact *s, int attr);
int pre_modify_service_attribute(struct service *s, int attr);
int pre_modify_host_attribute(struct host *h, int attr);
struct contact *get_premod_contact(unsigned int id);
struct host *get_premod_host(unsigned int id);
struct service *get_premod_service(unsigned int id);
void deinit_retention_data(void);
NAGIOS_END_DECL

#endif
