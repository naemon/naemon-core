#ifndef _CONFIGURATION_H
#define _CONFIGURATION_H

#include "lnae-utils.h"

NAGIOS_BEGIN_DECL

int read_main_config_file(char *);                     		/* reads the main config file (naemon.cfg) */
int read_resource_file(char *);					/* processes macros in resource file */
int read_all_object_data(char *);				/* reads all object config data */

int pre_flight_check(void);                          		/* try and verify the configuration data */
int pre_flight_object_check(int *, int *);               	/* verify object relationships and settings */
int pre_flight_circular_check(int *, int *);             	/* detects circular dependencies and paths */

NAGIOS_END_DECL

#endif
