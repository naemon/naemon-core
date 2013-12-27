#ifndef _SEHANDLERS_H
#define _SEHANDLERS_H

#include "objects.h"
#include "macros.h"

int obsessive_compulsive_service_check_processor(service *);	/* distributed monitoring craziness... */
int obsessive_compulsive_host_check_processor(host *);		/* distributed monitoring craziness... */
int handle_service_event(service *);				/* top level service event logic */
int run_service_event_handler(nagios_macros *mac, service *);			/* runs the event handler for a specific service */
int run_global_service_event_handler(nagios_macros *mac, service *);		/* runs the global service event handler */
int handle_host_event(host *);					/* top level host event logic */
int run_host_event_handler(nagios_macros *mac, host *);				/* runs the event handler for a specific host */
int run_global_host_event_handler(nagios_macros *mac, host *);			/* runs the global host event handler */

#endif
