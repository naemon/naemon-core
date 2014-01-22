#ifndef _SRETENTION_H
#define _SRETENTION_H

#include "common.h"
NAGIOS_BEGIN_DECL

int initialize_retention_data(const char *);
int cleanup_retention_data(void);
int save_state_information(int);                 /* saves all host and state information */
int read_initial_state_information(void);        /* reads in initial host and state information */
int pre_modify_service_attribute(struct service *s, int attr);
int pre_modify_host_attribute(struct host *h, int attr);
struct host *get_premod_host(unsigned int id);
struct service *get_premod_service(unsigned int id);
void deinit_retention_data(void);
NAGIOS_END_DECL

#endif
