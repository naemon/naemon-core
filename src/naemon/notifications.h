#ifndef _NOTIFICATIONS_H
#define _NOTIFICATIONS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects_serviceescalation.h"
#include "objects_hostescalation.h"
#include "macros.h"

/*
 * Enumeration of the type of objects that can influence notification
 * suppressions
 */
enum NotificationSuppressionType {
	NS_TYPE_HOST = 0,
	NS_TYPE_SERVICE,
	NS_TYPE_HOST_CONTACT,
	NS_TYPE_SERVICE_CONTACT,
	/* NOTE: NS_TYPE__COUNT assumes the types are sequential, and start at zero. If you break
	 * this assumption, you break this member as well and need to fix it
	 */
	NS_TYPE__COUNT
};

/*
 * Enumeration of all known valid notification suppression reasons
 */
enum NotificationSuppressionReason {
	NSR_OK,
	NSR_DISABLED,
	NSR_TIMEPERIOD_BLOCKED,
	NSR_DISABLED_OBJECT,
	NSR_NO_CONTACTS,
	NSR_CUSTOM_SCHED_DOWNTIME,
	NSR_ACK_OBJECT_OK,
	NSR_NO_FLAPPING,
	NSR_SCHED_DOWNTIME_FLAPPING,
	NSR_NO_DOWNTIME,
	NSR_SCHED_DOWNTIME_DOWNTIME,
	NSR_SOFT_STATE,
	NSR_ACKNOWLEDGED,
	NSR_DEPENDENCY_FAILURE,
	NSR_STATE_DISABLED,
	NSR_NO_RECOVERY,
	NSR_RECOVERY_UNNOTIFIED_PROBLEM,
	NSR_DELAY,
	NSR_IS_FLAPPING,
	NSR_IS_SCHEDULED_DOWNTIME,
	NSR_RE_NO_MORE,
	NSR_RE_NOT_YET,
	NSR_NEB_BLOCKED,
	NSR_BAD_PARENTS,
	NSR_SERVICE_HOST_DOWN_UNREACHABLE,
	NSR_SERVICE_HOST_SCHEDULED_DOWNTIME,
	NSR_INSUFF_IMPORTANCE,
};

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

typedef struct notify_list {
	struct contact *contact;
	struct notify_list *next;
} notification;

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
time_t get_next_host_notification_time(host *, time_t);				/* calculates next acceptable re-notification time for a host */
time_t get_next_service_notification_time(service *, time_t);			/* calculates next acceptable re-notification time for a service */

NAGIOS_END_DECL

#endif
