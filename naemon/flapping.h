#ifndef _FLAPPING_H
#define _FLAPPING_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"


#define HOST_FLAPPING                   0
#define SERVICE_FLAPPING                1

NAGIOS_BEGIN_DECL

void check_for_service_flapping(service *, int, int);	      /* determines whether or not a service is "flapping" between states */
void check_for_host_flapping(host *, int, int, int);		/* determines whether or not a host is "flapping" between states */
void set_service_flap(service *, double, double, double, int);	/* handles a service that is flapping */
void clear_service_flap(service *, double, double, double);	/* handles a service that has stopped flapping */
void set_host_flap(host *, double, double, double, int);		/* handles a host that is flapping */
void clear_host_flap(host *, double, double, double);		/* handles a host that has stopped flapping */
void enable_flap_detection_routines(void);			/* enables flap detection on a program-wide basis */
void disable_flap_detection_routines(void);			/* disables flap detection on a program-wide basis */
void enable_host_flap_detection(host *);			/* enables flap detection for a particular host */
void disable_host_flap_detection(host *);			/* disables flap detection for a particular host */
void enable_service_flap_detection(service *);			/* enables flap detection for a particular service */
void disable_service_flap_detection(service *);			/* disables flap detection for a particular service */
void handle_host_flap_detection_disabled(host *);		/* handles the details when flap detection is disabled globally or on a per-host basis */
void handle_service_flap_detection_disabled(service *);		/* handles the details when flap detection is disabled globally or on a per-service basis */

NAGIOS_END_DECL

#endif
