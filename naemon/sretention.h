#ifndef _SRETENTION_H
#define _SRETENTION_H

#include "common.h"
NAGIOS_BEGIN_DECL

int initialize_retention_data(const char *);
int cleanup_retention_data(void);
int save_state_information(int);                 /* saves all host and state information */
int read_initial_state_information(void);        /* reads in initial host and state information */

NAGIOS_END_DECL

#endif
