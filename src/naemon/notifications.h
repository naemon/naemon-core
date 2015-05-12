#ifndef _NOTIFICATIONS_H
#define _NOTIFICATIONS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"
#include "macros.h"


/**************** NOTIFICATION TYPES ******************/

#define HOST_NOTIFICATION               0
#define SERVICE_NOTIFICATION            1


/************* NOTIFICATION REASON TYPES ***************/
enum NotificationReason {
	NOTIFICATION_NORMAL,
	NOTIFICATION_ACKNOWLEDGEMENT,
	NOTIFICATION_FLAPPINGSTART,
	NOTIFICATION_FLAPPINGSTOP,
	NOTIFICATION_FLAPPINGDISABLED,
	NOTIFICATION_DOWNTIMESTART,
	NOTIFICATION_DOWNTIMEEND,
	NOTIFICATION_DOWNTIMECANCELLED,
	NOTIFICATION_CUSTOM
};

NAGIOS_BEGIN_DECL

const char *notification_reason_name(unsigned int reason_type);
int check_service_notification_viability(service *, int, int);			/* checks viability of notifying all contacts about a service */
int is_valid_escalation_for_service_notification(service *, serviceescalation *, int);	/* checks if an escalation entry is valid for a particular service notification */
int should_service_notification_be_escalated(service *);			/* checks if a service notification should be escalated */
int service_notification(service *, int, char *, char *, int);                     	/* notify all contacts about a service (problem or recovery) */
int check_contact_service_notification_viability(contact *, service *, int, int);	/* checks viability of notifying a contact about a service */
int notify_contact_of_service(nagios_macros *mac, contact *, service *, int, char *, char *, int, int);  	/* notify a single contact about a service */
int check_host_notification_viability(host *, int, int);				/* checks viability of notifying all contacts about a host */
int is_valid_escalation_for_host_notification(host *, hostescalation *, int);	/* checks if an escalation entry is valid for a particular host notification */
int should_host_notification_be_escalated(host *);				/* checks if a host notification should be escalated */
int host_notification(host *, int, char *, char *, int);                           	/* notify all contacts about a host (problem or recovery) */
int check_contact_host_notification_viability(contact *, host *, int, int);	/* checks viability of notifying a contact about a host */
int notify_contact_of_host(nagios_macros *mac, contact *, host *, int, char *, char *, int, int);        	/* notify a single contact about a host */
int create_notification_list_from_host(nagios_macros *mac, host *,int,int *,int);         		/* given a host, create list of contacts to be notified (remove duplicates) */
int create_notification_list_from_service(nagios_macros *mac, service *,int,int *,int);    		/* given a service, create list of contacts to be notified (remove duplicates) */
int add_notification(nagios_macros *mac, contact *);						/* adds a notification instance */
notification *find_notification(contact *);					/* finds a notification object */
time_t get_next_host_notification_time(host *, time_t);				/* calculates next acceptable re-notification time for a host */
time_t get_next_service_notification_time(service *, time_t);			/* calculates next acceptable re-notification time for a service */

NAGIOS_END_DECL

#endif
