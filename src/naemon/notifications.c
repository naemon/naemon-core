#include "notifications.h"
#include "config.h"
#include "common.h"
#include "statusdata.h"
#include "macros.h"
#include "broker.h"
#include "neberrors.h"
#include "nebmods.h"
#include "workers.h"
#include "utils.h"
#include "checks.h"
#include "checks_service.h"
#include "checks_host.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>

struct notification_job {
	host *hst;
	service *svc;
	contact *ctc;
};

static notification *create_notification_list_from_host(nagios_macros *mac, host *hst, int options, int *escalated, int type);
static notification *create_notification_list_from_service(nagios_macros *mac, service *svc, int options, int *escalated, int type);
static int add_notification(notification **notification_list, nagios_macros *mac, contact *);						/* adds a notification instance */

static void free_notification_list(notification *notification_list)
{
	notification *temp_notification = NULL;
	notification *next_notification = NULL;

	temp_notification = notification_list;
	while (temp_notification != NULL) {
		next_notification = temp_notification->next;
		nm_free(temp_notification);
		temp_notification = next_notification;
	}
}

/*** silly helpers ****/
static contact *find_contact_by_name_or_alias(const char *name)
{
	contact *c = NULL;

	if (!name || !(c = find_contact(name)))
		return c;
	for (c = contact_list; c; c = c->next)
		if (!strcmp(c->alias, name))
			break;

	return c;
}

const char *notification_reason_name(enum NotificationReason reason)
{
	switch (reason) {
	case NOTIFICATION_NORMAL:
		return "NORMAL";
		break;
	case NOTIFICATION_ACKNOWLEDGEMENT:
		return "ACKNOWLEDGEMENT";
		break;
	case NOTIFICATION_FLAPPINGSTART:
		return "FLAPPINGSTART";
		break;
	case NOTIFICATION_FLAPPINGSTOP:
		return "FLAPPINGSTOP";
		break;
	case NOTIFICATION_FLAPPINGDISABLED:
		return "FLAPPINGDISABLED";
		break;
	case NOTIFICATION_DOWNTIMESTART:
		return "DOWNTIMESTART";
		break;
	case NOTIFICATION_DOWNTIMEEND:
		return "DOWNTIMEEND";
		break;
	case NOTIFICATION_DOWNTIMECANCELLED:
		return "DOWNTIMECANCELLED";
		break;
	case NOTIFICATION_CUSTOM:
		return "CUSTOM";
		break;
	}
	nm_log(NSLOG_RUNTIME_ERROR, "Unhandled notification reason: %d", reason);
	return NULL;

}


/*
 * Keep track of/update the notification suppression reason for a given object.
 * Returns TRUE if the new reason is an update, and FALSE if it is not.
 */
static int update_notification_suppression_reason(enum NotificationSuppressionType type, unsigned int obj_id,
        enum NotificationSuppressionReason reason)
{
	/*
	 * NOTE:
	 * This map only ever grows currently, since we never delete objects. It
	 * doesn't allocate everything at once (based on whatever the highest ID
	 * is, for example), since this way makes it easier to accommodate dynamic
	 * object configuration. At least it doesn't make it significantly harder.
	 */
	static struct {
		unsigned int count;
		enum NotificationSuppressionReason *reasons;
	} nsr_map[NS_TYPE__COUNT];
	unsigned int new_count;

	/* object id:s start at zero */
	new_count = obj_id + 1;
	if (nsr_map[type].count < new_count) {
		nsr_map[type].reasons = nm_realloc(nsr_map[type].reasons,
		                                   new_count * sizeof(reason));

		memset(nsr_map[type].reasons + nsr_map[type].count, (int) NSR_OK,
		       (new_count - nsr_map[type].count) * sizeof(reason));
		nsr_map[type].count = new_count;
	}

	if (nsr_map[type].reasons[obj_id] != reason) {
		nsr_map[type].reasons[obj_id] = reason;
		return TRUE;
	}
	return FALSE;
}

#define _log_nsr(S) if (enable_notification_suppression_reason_logging) { \
	if (update_notification_suppression_reason(type, objid, reason)) { \
		nm_log(log_level, "%s NOTIFICATION SUPPRESSED: %s;%s", type_name, objname, S); \
	} else { log_debug_info(DEBUGL_NOTIFICATIONS, DEBUGV_BASIC, "%s NOTIFICATION SUPPRESSED: %s;%s\n", type_name, objname, S);}}
void log_notification_suppression_reason(enum NotificationSuppressionReason reason,
        enum NotificationSuppressionType type, void *primary_obj, void *secondary_obj, const char *extra_info)
{
	char *objname = NULL;
	char *type_name = NULL;
	unsigned int objid = 0u;
	unsigned int log_level = NSLOG_RUNTIME_ERROR;
	unsigned int modattr = 0u;

	switch (type) {
	case NS_TYPE__COUNT: break;
	case NS_TYPE_HOST:
		objname = nm_strdup(((host *)primary_obj)->name);
		objid = ((host *)primary_obj)->id;
		type_name = "HOST";
		log_level = NSLOG_HOST_NOTIFICATION;
		modattr = ((struct host *)primary_obj)->modified_attributes;
		break;
	case NS_TYPE_SERVICE:
		nm_asprintf(&objname, "%s;%s", ((service *)primary_obj)->host_name, ((service *)primary_obj)->description);
		objid = ((service *)primary_obj)->id;
		type_name = "SERVICE";
		log_level = NSLOG_SERVICE_NOTIFICATION;
		modattr = ((struct service *)primary_obj)->modified_attributes;
		break;
	case NS_TYPE_SERVICE_CONTACT:
		nm_asprintf(&objname, "%s;%s;%s",
		            ((contact *)primary_obj)->name,
		            ((service *)secondary_obj)->host_name,
		            ((service *)secondary_obj)->description
		           );
		objid = ((contact *)primary_obj)->id;
		type_name = "SERVICE CONTACT";
		log_level = NSLOG_SERVICE_NOTIFICATION;
		modattr = ((struct service *)secondary_obj)->modified_attributes;
		break;
	case NS_TYPE_HOST_CONTACT:
		nm_asprintf(&objname, "%s;%s",
		            ((contact *)primary_obj)->name,
		            ((host *)secondary_obj)->name
		           );
		objid = ((contact *)primary_obj)->id;
		type_name = "HOST CONTACT";
		log_level = NSLOG_HOST_NOTIFICATION;
		modattr = ((struct host *)secondary_obj)->modified_attributes;
		break;
	}

	switch (reason) {
	case NSR_OK:
		break;
	case NSR_DISABLED:
		_log_nsr("Notifications are disabled globally.");
		break;
	case NSR_TIMEPERIOD_BLOCKED:
		_log_nsr("Notification blocked by timeperiod; notifications should not be sent out at this time.");
		break;
	case NSR_DISABLED_OBJECT:
		if (modattr & MODATTR_NOTIFICATIONS_ENABLED) {
			_log_nsr("Notifications are temporarily disabled for this object by an external command.");
		} else {
			_log_nsr("Notifications are disabled for this object by its configuration.");
		}
		break;
	case NSR_NO_CONTACTS:
		_log_nsr("No notification sent, because no contacts were found for notification purposes.");
		break;
	case NSR_CUSTOM_SCHED_DOWNTIME:
		_log_nsr("Custom notifications blocked during scheduled downtime.");
		break;
	case NSR_ACK_OBJECT_OK:
		_log_nsr("Acknowledgement notification blocked for UP/OK object.");
		break;
	case NSR_NO_FLAPPING:
		_log_nsr("Notifications about FLAPPING events blocked for this object.");
		break;
	case NSR_SCHED_DOWNTIME_FLAPPING:
		_log_nsr("Notifications about FLAPPING events blocked during scheduled downtime.");
		break;
	case NSR_NO_DOWNTIME:
		_log_nsr("Notifications about SCHEDULED DOWNTIME events blocked for this object.");
		break;
	case NSR_SCHED_DOWNTIME_DOWNTIME:
		_log_nsr("Notifications about SCHEDULED DOWNTIME events blocked during scheduled downtime.");
		break;
	case NSR_SOFT_STATE:
		_log_nsr("Notifications blocked for object in a soft state.");
		break;
	case NSR_ACKNOWLEDGED:
		_log_nsr("Notification for problem blocked because it has already been acknowledged.");
		break;
	case NSR_DEPENDENCY_FAILURE:
		_log_nsr("Notification blocked due to dependency of another object.");
		break;
	case NSR_STATE_DISABLED:
		_log_nsr("Notifications disabled for current object state.");
		break;
	case NSR_NO_RECOVERY:
		_log_nsr("Notifications about RECOVERY events blocked for this object.");
		break;
	case NSR_RECOVERY_UNNOTIFIED_PROBLEM:
		_log_nsr("Notification blocked for RECOVERY because no notification was sent out for the original problem.");
		break;
	case NSR_DELAY:
		_log_nsr("Notification blocked because first_notification_delay is configured and not enough time has elapsed since the object changed to a non-UP/non-OK state (or since program start).");
		break;
	case NSR_IS_FLAPPING:
		_log_nsr("Notification blocked because the object is currently flapping.");
		break;
	case NSR_IS_SCHEDULED_DOWNTIME:
		_log_nsr("Notification blocked for object currently in a scheduled downtime.");
		break;
	case NSR_RE_NO_MORE:
		_log_nsr("Re-notification blocked for this problem.");
		break;
	case NSR_RE_NOT_YET:
		_log_nsr("Re-notification blocked for this problem because not enough time has passed since last notification.");
		break;
	case NSR_NEB_BLOCKED: {
		char *s = NULL;
		nm_asprintf(&s, "Notification was blocked by a NEB module. %s", extra_info);
		_log_nsr(s);
		nm_free(s);
	}
	break;
	case NSR_BAD_PARENTS:
		_log_nsr("Notification blocked because this object is unreachable - its parents are down.");
		break;
	case NSR_SERVICE_HOST_DOWN_UNREACHABLE:
		_log_nsr("Notification blocked for service because its associated host is either down or unreachable.");
		break;
	case NSR_SERVICE_HOST_SCHEDULED_DOWNTIME:
		_log_nsr("Notification blocked for service because its associated host is currently in a scheduled downtime.");
		break;
	case NSR_INSUFF_IMPORTANCE:
		_log_nsr("Notification blocked for contact because it is not important enough (according to minimum_value).");
		break;

	}
	nm_free(objname);
}
#undef _log_nsr

#define LOG_SERVICE_NSR(NSR) log_notification_suppression_reason(NSR, NS_TYPE_SERVICE, svc, NULL, NULL);
#define LOG_HOST_NSR(NSR) log_notification_suppression_reason(NSR, NS_TYPE_HOST, hst, NULL, NULL);
#define LOG_SERVICE_CONTACT_NSR(NSR) log_notification_suppression_reason(NSR, NS_TYPE_SERVICE_CONTACT, cntct, svc, NULL);
#define LOG_HOST_CONTACT_NSR(NSR) log_notification_suppression_reason(NSR, NS_TYPE_HOST_CONTACT, cntct, hst, NULL);
#define LOG_SERVICE_NSR_NEB_BLOCKED(NSR, NEB_CB_DESCRIPTION) log_notification_suppression_reason(NSR, NS_TYPE_SERVICE, svc, NULL, NEB_CB_DESCRIPTION);
#define LOG_HOST_NSR_NEB_BLOCKED(NSR, NEB_CB_DESCRIPTION) log_notification_suppression_reason(NSR, NS_TYPE_HOST, hst, NULL, NEB_CB_DESCRIPTION);

static void notification_handle_job_result(struct wproc_result *wpres, void *data, int flags)
{
	struct notification_job *nj = (struct notification_job *)data;
	if (wpres) {
		if (wpres->early_timeout) {
			if (nj->svc) {
				nm_log(NSLOG_RUNTIME_WARNING, "Warning: Timeout while notifying contact '%s' of service '%s' on host '%s' by command '%s'\n",
				       nj->ctc->name, nj->svc->description,
				       nj->hst->name, wpres->command);
			} else {
				nm_log(NSLOG_RUNTIME_WARNING, "Warning: Timeout while notifying contact '%s' of host '%s' by command '%s'\n",
				       nj->ctc->name, nj->hst->name,
				       wpres->command);
			}
		} else if (!WIFEXITED(wpres->wait_status) || WEXITSTATUS(wpres->wait_status)) {
			char *objectname = NULL;
			char *reason = NULL;
			char *objecttype = NULL;
			int code = 0;
			if (nj->svc) {
				objecttype = "service";
				nm_asprintf(&objectname, "%s;%s", nj->svc->host_name, nj->svc->description);
			} else {
				objecttype = "host";
				objectname = strdup(nj->hst->name);
			}
			if (!WIFEXITED(wpres->wait_status)) {
				reason = "was killed by signal";
				code = WTERMSIG(wpres->wait_status);
			} else {
				reason = "exited with exit code";
				code = WEXITSTATUS(wpres->wait_status);
			}
			nm_log(NSLOG_RUNTIME_WARNING,
			       "Warning: Notification command for contact '%s' about %s '%s' %s %i. stdout: '%s', stderr: '%s'",
			       nj->ctc->name,
			       objecttype,
			       objectname,
			       reason,
			       code,
			       wpres->outstd && wpres->outstd[0] ? wpres->outstd : "(empty)",
			       wpres->outerr && wpres->outerr[0] ? wpres->outerr : "(empty)");
			free(objectname);
		}
	}
	free(nj);
}


/******************************************************************/
/***************** SERVICE NOTIFICATION FUNCTIONS *****************/
/******************************************************************/

/* notify contacts about a service problem or recovery */
int service_notification(service *svc, int type, char *not_author, char *not_data, int options)
{
	notification *notification_list = NULL;
	notification *temp_notification = NULL;
	contact *temp_contact = NULL;
	time_t current_time;
	struct timeval start_time;
	struct timeval end_time;
	int escalated = FALSE;
	int result = OK;
	int contacts_notified = 0;
	int increment_notification_number = FALSE;
	nagios_macros mac;
	neb_cb_resultset *neb_resultset = NULL;
	neb_cb_resultset_iter neb_resultset_iter;
	neb_cb_result *neb_result = NULL;

	/* get the current time */
	time(&current_time);
	gettimeofday(&start_time, NULL);

	log_debug_info(DEBUGL_NOTIFICATIONS, 0, "** Service Notification Attempt ** Host: '%s', Service: '%s', Type: %s, Options: %d, Current State: %d, Last Notification: %s\n", svc->host_name, svc->description, notification_reason_name(type), options, svc->current_state, ctime(&svc->last_notification));


	/* check the viability of sending out a service notification */
	if (check_service_notification_viability(svc, type, options) == ERROR) {
		log_debug_info(DEBUGL_NOTIFICATIONS, 0, "Notification viability test failed.  No notification will be sent out.\n");
		return OK;
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 0, "Notification viability test passed.\n");

	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_resultset = broker_notification_data(NEBTYPE_NOTIFICATION_START, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_NOTIFICATION, type, start_time, end_time, (void *)svc, not_author, not_data, escalated, 0);
	neb_cb_resultset_iter_init(&neb_resultset_iter, neb_resultset);
	while (neb_cb_resultset_iter_next(&neb_resultset_iter, &neb_result)) {
		int rc = neb_cb_result_returncode(neb_result);

		if (rc == NEBERROR_CALLBACKCANCEL || rc == NEBERROR_CALLBACKOVERRIDE) {
			char *description = NULL;
			nm_asprintf(&description, "Module '%s' %s: '%s'",
			            neb_cb_result_module_name(neb_result),
			            rc == NEBERROR_CALLBACKCANCEL ? "cancelled notification" : "overrode notification",
			            neb_cb_result_description(neb_result));
			LOG_SERVICE_NSR_NEB_BLOCKED(NSR_NEB_BLOCKED, description);
			nm_free(description);
			neb_cb_resultset_destroy(neb_resultset);
			return rc == NEBERROR_CALLBACKCANCEL ? ERROR : OK;
		}
	}
	neb_cb_resultset_destroy(neb_resultset);

	/* should the notification number be increased? */
	if (type == NOTIFICATION_NORMAL || (options & NOTIFICATION_OPTION_INCREMENT)) {
		svc->current_notification_number++;
		increment_notification_number = TRUE;
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Current notification number: %d (%s)\n", svc->current_notification_number, (increment_notification_number == TRUE) ? "incremented" : "changed");

	/* save and increase the current notification id */
	nm_free(svc->current_notification_id);
	svc->current_notification_id = g_uuid_string_random();

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Creating list of contacts to be notified.\n");

	/* allocate memory for macros */
	memset(&mac, 0, sizeof(mac));

	/* create the contact notification list for this service */

	/*
	 * check viability before adding a contact to the notification
	 * list and build up the $NOTIFICATIONRECIPIENTS$ macro while
	 * we're at it.
	 * This prevents us from running through all the steps again in
	 * notify_contact_of_host|service.
	 * Furthermore the $NOTIFICATIONRECIPIENTS$ macro will contain
	 * only actual recipients (as the name implies), and not all
	 * contacts assigned to that host|service.
	 *
	 * note: checks against timeperiod will happen now(),
	 * and not when the notification is actually being sent.
	 */
	notification_list = create_notification_list_from_service(&mac, svc, options, &escalated, type);

	/* we have contacts to notify... */
	if (notification_list != NULL) {

		/* grab the macro variables */
		grab_service_macros_r(&mac, svc);

		/* if this notification has an author, attempt to lookup the associated contact */
		if (not_author != NULL) {
			temp_contact = find_contact_by_name_or_alias(not_author);
		}

		/* get author and comment macros */
		if (not_author)
			mac.x[MACRO_NOTIFICATIONAUTHOR] = nm_strdup(not_author);
		if (temp_contact != NULL) {
			mac.x[MACRO_NOTIFICATIONAUTHORNAME] = nm_strdup(temp_contact->name);
			mac.x[MACRO_NOTIFICATIONAUTHORALIAS] = nm_strdup(temp_contact->alias);
		}
		if (not_data)
			mac.x[MACRO_NOTIFICATIONCOMMENT] = nm_strdup(not_data);

		/* NOTE: these macros are deprecated and will likely disappear in Nagios 4.x */
		/* if this is an acknowledgement, get author and comment macros */
		if (type == NOTIFICATION_ACKNOWLEDGEMENT) {
			if (not_author)
				mac.x[MACRO_SERVICEACKAUTHOR] = nm_strdup(not_author);

			if (not_data)
				mac.x[MACRO_SERVICEACKCOMMENT] = nm_strdup(not_data);

			if (temp_contact != NULL) {
				mac.x[MACRO_SERVICEACKAUTHORNAME] = nm_strdup(temp_contact->name);
				mac.x[MACRO_SERVICEACKAUTHORALIAS] = nm_strdup(temp_contact->alias);
			}
		}

		/* set the notification type macro */
		if (type != NOTIFICATION_NORMAL) {
			mac.x[MACRO_NOTIFICATIONTYPE] = nm_strdup(notification_reason_name(type));
		} else if (svc->current_state == STATE_OK) {
			mac.x[MACRO_NOTIFICATIONTYPE] = nm_strdup("RECOVERY");
		} else {
			mac.x[MACRO_NOTIFICATIONTYPE] = nm_strdup("PROBLEM");
		}


		/* set the notification number macro */
		nm_asprintf(&mac.x[MACRO_SERVICENOTIFICATIONNUMBER], "%d", svc->current_notification_number);

		/* the $NOTIFICATIONNUMBER$ macro is maintained for backward compatibility */
		mac.x[MACRO_NOTIFICATIONNUMBER] = nm_strdup(mac.x[MACRO_SERVICENOTIFICATIONNUMBER]);

		/* set the notification id macro */
		nm_asprintf(&mac.x[MACRO_SERVICENOTIFICATIONID], "%s", svc->current_notification_id);

		/* notify each contact (duplicates have been removed) */
		for (temp_notification = notification_list; temp_notification != NULL; temp_notification = temp_notification->next) {

			/* grab the macro variables for this contact */
			grab_contact_macros_r(&mac, temp_notification->contact);

			/* notify this contact */
			result = notify_contact_of_service(&mac, temp_notification->contact, svc, type, not_author, not_data, options, escalated);

			/* keep track of how many contacts were notified */
			if (result == OK)
				contacts_notified++;
		}

		free_notification_list(notification_list);

		/* clear out all macros we created */
		nm_free(mac.x[MACRO_NOTIFICATIONNUMBER]);
		nm_free(mac.x[MACRO_SERVICENOTIFICATIONNUMBER]);
		nm_free(mac.x[MACRO_SERVICENOTIFICATIONID]);
		nm_free(mac.x[MACRO_NOTIFICATIONCOMMENT]);
		nm_free(mac.x[MACRO_NOTIFICATIONTYPE]);
		nm_free(mac.x[MACRO_NOTIFICATIONAUTHOR]);
		nm_free(mac.x[MACRO_NOTIFICATIONAUTHORNAME]);
		nm_free(mac.x[MACRO_NOTIFICATIONAUTHORALIAS]);
		nm_free(mac.x[MACRO_SERVICEACKAUTHORNAME]);
		nm_free(mac.x[MACRO_SERVICEACKAUTHORALIAS]);
		nm_free(mac.x[MACRO_SERVICEACKAUTHOR]);
		nm_free(mac.x[MACRO_SERVICEACKCOMMENT]);

		/* this gets set in add_notification() */
		nm_free(mac.x[MACRO_NOTIFICATIONRECIPIENTS]);

		/*
		 * Clear all macros, or they will linger in memory
		 * now that we're done with the notifications.
		 */
		clear_summary_macros_r(&mac);
		clear_contact_macros_r(&mac);
		clear_argv_macros_r(&mac);
		clear_host_macros_r(&mac);
		clear_service_macros_r(&mac);

		if (type == NOTIFICATION_NORMAL) {

			/* adjust last/next notification time and notification flags if we notified someone */
			if (contacts_notified > 0) {

				/* calculate the next acceptable re-notification time */
				svc->next_notification = get_next_service_notification_time(svc, current_time);

				log_debug_info(DEBUGL_NOTIFICATIONS, 0, "%d contacts were notified.  Next possible notification time: %s\n", contacts_notified, ctime(&svc->next_notification));

				/* update the last notification time for this service (this is needed for rescheduling later notifications) */
				svc->last_notification = current_time;

				/* update notifications flags */
				add_notified_on(svc, svc->current_state);
				svc->last_update = current_time;
			}

			/* we didn't end up notifying anyone */
			else if (increment_notification_number == TRUE) {

				/* adjust current notification number */
				svc->current_notification_number--;

				log_debug_info(DEBUGL_NOTIFICATIONS, 0, "No contacts were notified.  Next possible notification time: %s\n", ctime(&svc->next_notification));
				svc->last_update = current_time;
			}
		}

		log_debug_info(DEBUGL_NOTIFICATIONS, 0, "%d contacts were notified.\n", contacts_notified);
	}

	/* there were no contacts, so no notification really occurred... */
	else {

		/* readjust current notification number, since one didn't go out */
		if (increment_notification_number == TRUE)
			svc->current_notification_number--;

		LOG_SERVICE_NSR(NSR_NO_CONTACTS);
	}

	/* this gets set in create_notification_list_from_service() */
	nm_free(mac.x[MACRO_NOTIFICATIONISESCALATED]);

	/* get the time we finished */
	gettimeofday(&end_time, NULL);

	neb_resultset = broker_notification_data(NEBTYPE_NOTIFICATION_END, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_NOTIFICATION, type, start_time, end_time, (void *)svc, not_author, not_data, escalated, contacts_notified);
	neb_cb_resultset_destroy(neb_resultset);

	/* update the status log with the service information */
	update_service_status(svc, FALSE);

	return OK;
}


/* checks the viability of sending out a service alert (top level filters) */
int check_service_notification_viability(service *svc, int type, int options)
{
	host *temp_host;
	timeperiod *temp_period;
	time_t current_time;
	time_t timeperiod_start;
	time_t first_problem_time;
	servicesmember *sm;

	/* forced notifications bust through everything */
	if (options & NOTIFICATION_OPTION_FORCED) {
		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "This is a forced service notification, so we'll send it out.\n");
		return OK;
	}

	/* get current time */
	time(&current_time);

	/* are notifications enabled? */
	if (enable_notifications == FALSE) {
		LOG_SERVICE_NSR(NSR_DISABLED);
		return ERROR;
	}

	temp_host = svc->host_ptr;

	/* if the service has no notification period, inherit one from the host */
	temp_period = svc->notification_period_ptr;
	if (temp_period == NULL) {
		temp_period = svc->host_ptr->notification_period_ptr;
	}

	/* see if the service can have notifications sent out at this time */
	if (check_time_against_period(current_time, temp_period) == ERROR) {
		LOG_SERVICE_NSR(NSR_TIMEPERIOD_BLOCKED);
		/* calculate the next acceptable notification time, once the next valid time range arrives... */
		if (type == NOTIFICATION_NORMAL) {

			get_next_valid_time(current_time, &timeperiod_start, svc->notification_period_ptr);

			/* looks like there are no valid notification times defined, so schedule the next one far into the future (one year)... */
			if (timeperiod_start == (time_t)0)
				svc->next_notification = (time_t)(current_time + (60 * 60 * 24 * 365));

			/* else use the next valid notification time */
			else
				svc->next_notification = timeperiod_start;

			log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Next possible notification time: %s\n", ctime(&svc->next_notification));
		}

		return ERROR;
	}

	/* are notifications temporarily disabled for this service? */
	if (svc->notifications_enabled == FALSE) {
		LOG_SERVICE_NSR(NSR_DISABLED_OBJECT);
		return ERROR;
	}


	/*********************************************/
	/*** SPECIAL CASE FOR CUSTOM NOTIFICATIONS ***/
	/*********************************************/

	/* custom notifications are good to go at this point... */
	if (type == NOTIFICATION_CUSTOM) {
		if (svc->scheduled_downtime_depth > 0 || temp_host->scheduled_downtime_depth > 0) {
			LOG_SERVICE_NSR(NSR_CUSTOM_SCHED_DOWNTIME);
			return ERROR;
		}
		return OK;
	}


	/****************************************/
	/*** SPECIAL CASE FOR ACKNOWLEGEMENTS ***/
	/****************************************/

	/* acknowledgements only have to pass three general filters, although they have another test of their own... */
	if (type == NOTIFICATION_ACKNOWLEDGEMENT) {

		/* don't send an acknowledgement if there isn't a problem... */
		if (svc->current_state == STATE_OK) {
			LOG_SERVICE_NSR(NSR_ACK_OBJECT_OK);
			return ERROR;
		}

		/* acknowledgement viability test passed, so the notification can be sent out */
		return OK;
	}

	/****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/****************************************/

	/* flapping notifications only have to pass three general filters */
	if (type == NOTIFICATION_FLAPPINGSTART || type == NOTIFICATION_FLAPPINGSTOP || type == NOTIFICATION_FLAPPINGDISABLED) {

		/* don't send a notification if we're not supposed to... */
		if (flag_isset(svc->notification_options, OPT_FLAPPING) == FALSE) {
			LOG_SERVICE_NSR(NSR_NO_FLAPPING);
			return ERROR;
		}

		/* don't send notifications during scheduled downtime */
		if (svc->scheduled_downtime_depth > 0 || temp_host->scheduled_downtime_depth > 0) {
			LOG_SERVICE_NSR(NSR_SCHED_DOWNTIME_FLAPPING);
			return ERROR;
		}

		/* flapping viability test passed, so the notification can be sent out */
		return OK;
	}


	/****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/****************************************/

	/* downtime notifications only have to pass three general filters */
	if (type == NOTIFICATION_DOWNTIMESTART || type == NOTIFICATION_DOWNTIMEEND || type == NOTIFICATION_DOWNTIMECANCELLED) {

		/* don't send a notification if we're not supposed to... */
		if (flag_isset(svc->notification_options, OPT_DOWNTIME) == FALSE) {
			LOG_SERVICE_NSR(NSR_NO_DOWNTIME);
			return ERROR;
		}

		/* don't send notifications during scheduled downtime (for service only, not host) */
		if (svc->scheduled_downtime_depth > 0) {
			LOG_SERVICE_NSR(NSR_SCHED_DOWNTIME_DOWNTIME);
			return ERROR;
		}

		/* downtime viability test passed, so the notification can be sent out */
		return OK;
	}

	/******************************************************/
	/*** CHECK SERVICE PARENTS FOR NORMAL NOTIFICATIONS ***/
	/******************************************************/
	/* if all parents are bad (usually just one), we shouldn't notify */
	/* but do not prevent recovery notifications */
	if (svc->parents && svc->current_state != STATE_OK) {
		sm = svc->parents;
		while (sm && sm->service_ptr->current_state != STATE_OK) {
			sm = sm->next;
		}
		if (sm == NULL) {
			LOG_SERVICE_NSR(NSR_BAD_PARENTS);
			return ERROR;
		}
	}

	/****************************************/
	/*** NORMAL NOTIFICATIONS ***************/
	/****************************************/

	/* is this a hard problem/recovery? */
	if (svc->state_type == SOFT_STATE) {
		LOG_SERVICE_NSR(NSR_SOFT_STATE);
		return ERROR;
	}

	/* has this problem already been acknowledged? */
	if (svc->problem_has_been_acknowledged == TRUE) {
		LOG_SERVICE_NSR(NSR_ACKNOWLEDGED);
		return ERROR;
	}

	/* check service notification dependencies */
	if (check_service_dependencies(svc, NOTIFICATION_DEPENDENCY) == DEPENDENCIES_FAILED) {
		LOG_SERVICE_NSR(NSR_DEPENDENCY_FAILURE);
		return ERROR;
	}

	/* check host notification dependencies */
	if (check_host_dependencies(temp_host, NOTIFICATION_DEPENDENCY) == DEPENDENCIES_FAILED) {
		LOG_SERVICE_NSR(NSR_DEPENDENCY_FAILURE);
		return ERROR;
	}

	/* see if we should notify about problems with this service */
	if (should_notify(svc) == FALSE) {
		LOG_SERVICE_NSR(NSR_STATE_DISABLED);
		return ERROR;
	}

	if (svc->current_state == STATE_OK && svc->notified_on == 0) {
		LOG_SERVICE_NSR(NSR_RECOVERY_UNNOTIFIED_PROBLEM);
		return ERROR;
	}


	/* see if enough time has elapsed for first notification (Mathias Sundman) */
	/* 10/02/07 don't place restrictions on recoveries or non-normal notifications, must use last time ok (or program start) in calculation */
	/* it is reasonable to assume that if the service was never up, the program start time should be used in this calculation */
	if (type == NOTIFICATION_NORMAL
	    && svc->current_notification_number == 0
	    && svc->first_notification_delay > 0
	    && svc->current_state != STATE_OK) {

		first_problem_time = svc->last_hard_state_change > 0 ? svc->last_hard_state_change : program_start;

		if (current_time < first_problem_time + (time_t)(svc->first_notification_delay * interval_length)) {
			LOG_SERVICE_NSR(NSR_DELAY);
			return ERROR;
		}
	}

	/* if this service is currently flapping, don't send the notification */
	if (enable_flap_detection == TRUE && svc->flap_detection_enabled == TRUE && svc->is_flapping == TRUE) {
		LOG_SERVICE_NSR(NSR_IS_FLAPPING);
		return ERROR;
	}

	/***** RECOVERY NOTIFICATIONS ARE GOOD TO GO AT THIS POINT *****/
	if (svc->current_state == STATE_OK)
		return OK;

	/* don't notify contacts about this service problem again if the notification interval is set to 0 */
	if (svc->no_more_notifications == TRUE) {
		LOG_SERVICE_NSR(NSR_RE_NO_MORE);
		return ERROR;
	}

	/* if the host is down or unreachable, don't notify contacts about service failures */
	if (temp_host->current_state != STATE_UP) {
		LOG_SERVICE_NSR(NSR_SERVICE_HOST_DOWN_UNREACHABLE);
		return ERROR;
	}

	/* don't notify if we haven't waited long enough since the last time (and the service is not marked as being volatile) */
	if ((current_time < svc->next_notification) && svc->is_volatile == FALSE) {
		LOG_SERVICE_NSR(NSR_RE_NOT_YET);
		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Next valid notification time: %s\n", ctime(&svc->next_notification));
		return ERROR;
	}

	/* if this service is currently in a scheduled downtime period, don't send the notification */
	if (svc->scheduled_downtime_depth > 0) {
		LOG_SERVICE_NSR(NSR_IS_SCHEDULED_DOWNTIME);
		return ERROR;
	}

	/* if this host is currently in a scheduled downtime period, don't send the notification */
	if (temp_host->scheduled_downtime_depth > 0) {
		LOG_SERVICE_NSR(NSR_SERVICE_HOST_SCHEDULED_DOWNTIME);
		return ERROR;
	}

	return OK;
}


/* check viability of sending out a service notification to a specific contact (contact-specific filters) */
int check_contact_service_notification_viability(contact *cntct, service *svc, int type, int options)
{

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "** Checking service notification viability for contact '%s'...\n", cntct->name);

	/* forced notifications bust through everything */
	if (options & NOTIFICATION_OPTION_FORCED) {
		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "This is a forced service notification, so we'll send it out to this contact.\n");
		return OK;
	}

	/* is this service not important enough? */
	if (cntct->minimum_value > svc->hourly_value) {
		LOG_SERVICE_CONTACT_NSR(NSR_INSUFF_IMPORTANCE);
		return ERROR;
	}

	/* are notifications enabled? */
	if (cntct->service_notifications_enabled == FALSE) {
		LOG_SERVICE_CONTACT_NSR(NSR_DISABLED_OBJECT);
		return ERROR;
	}

	/* see if the contact can be notified at this time */
	if (check_time_against_period(time(NULL), cntct->service_notification_period_ptr) == ERROR) {
		LOG_SERVICE_CONTACT_NSR(NSR_TIMEPERIOD_BLOCKED);
		return ERROR;
	}

	/*********************************************/
	/*** SPECIAL CASE FOR CUSTOM NOTIFICATIONS ***/
	/*********************************************/

	/* custom notifications are good to go at this point... */
	if (type == NOTIFICATION_CUSTOM)
		return OK;


	/****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/****************************************/

	if (type == NOTIFICATION_FLAPPINGSTART || type == NOTIFICATION_FLAPPINGSTOP || type == NOTIFICATION_FLAPPINGDISABLED) {

		if ((cntct->service_notification_options & OPT_FLAPPING) == FALSE) {
			LOG_SERVICE_CONTACT_NSR(NSR_NO_FLAPPING);
			return ERROR;
		}

		return OK;
	}

	/****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/****************************************/

	if (type == NOTIFICATION_DOWNTIMESTART || type == NOTIFICATION_DOWNTIMEEND || type == NOTIFICATION_DOWNTIMECANCELLED) {

		if ((cntct->service_notification_options & OPT_DOWNTIME) == FALSE) {
			LOG_SERVICE_CONTACT_NSR(NSR_NO_DOWNTIME);
			return ERROR;
		}

		return OK;
	}

	/*************************************/
	/*** ACKS AND NORMAL NOTIFICATIONS ***/
	/*************************************/

	/* see if we should notify about problems with this service */
	if (!(cntct->service_notification_options & (1 << svc->current_state))) {
		LOG_SERVICE_CONTACT_NSR(NSR_STATE_DISABLED);
		return ERROR;
	}

	if (svc->current_state == STATE_OK) {

		if ((cntct->service_notification_options & OPT_RECOVERY) == FALSE) {
			LOG_SERVICE_CONTACT_NSR(NSR_NO_RECOVERY);
			return ERROR;
		}

		if (!(svc->notified_on & cntct->service_notification_options)) {
			LOG_SERVICE_CONTACT_NSR(NSR_RECOVERY_UNNOTIFIED_PROBLEM);
			return ERROR;
		}
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "** Service notification viability for contact '%s' PASSED.\n", cntct->name);

	return OK;
}


/* notify a specific contact about a service problem or recovery */
int notify_contact_of_service(nagios_macros *mac, contact *cntct, service *svc, int type, char *not_author, char *not_data, int options, int escalated)
{
	commandsmember *temp_commandsmember = NULL;
	char *command_name = NULL;
	char *command_name_ptr = NULL;
	char *raw_command = NULL;
	char *processed_command = NULL;
	char *temp_buffer = NULL;
	char *processed_buffer = NULL;
	struct timeval start_time, end_time;
	struct timeval method_start_time, method_end_time;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	int neb_result;
	struct notification_job *nj;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "** Notifying contact '%s'\n", cntct->name);

	/* get start time */
	gettimeofday(&start_time, NULL);

	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_result = broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_START, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_NOTIFICATION, type, start_time, end_time, (void *)svc, cntct, not_author, not_data, escalated);
	if (NEBERROR_CALLBACKCANCEL == neb_result)
		return ERROR;
	else if (NEBERROR_CALLBACKOVERRIDE == neb_result)
		return OK;

	/* process all the notification commands this user has */
	for (temp_commandsmember = cntct->service_notification_commands; temp_commandsmember != NULL; temp_commandsmember = temp_commandsmember->next) {

		/* get start time */
		gettimeofday(&method_start_time, NULL);

		method_end_time.tv_sec = 0L;
		method_end_time.tv_usec = 0L;
		neb_result = broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_START, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_NOTIFICATION, type, method_start_time, method_end_time, (void *)svc, cntct, temp_commandsmember->command, not_author, not_data, escalated);
		if (NEBERROR_CALLBACKCANCEL == neb_result)
			break ;
		else if (NEBERROR_CALLBACKOVERRIDE == neb_result)
			continue ;

		get_raw_command_line_r(mac, temp_commandsmember->command_ptr, temp_commandsmember->command, &raw_command, macro_options);
		if (raw_command == NULL)
			continue;

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Raw notification command: %s\n", raw_command);

		/* process any macros contained in the argument */
		process_macros_r(mac, raw_command, &processed_command, macro_options);
		nm_free(raw_command);
		if (processed_command == NULL)
			continue;

		/* get the command name */
		command_name = nm_strdup(temp_commandsmember->command);
		command_name_ptr = strtok(command_name, "!");

		/* run the notification command... */

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Processed notification command: %s\n", processed_command);

		/* log the notification to program log file */
		if (log_notifications == TRUE) {
			if (type != NOTIFICATION_NORMAL) {
				nm_asprintf(&temp_buffer, "SERVICE NOTIFICATION: %s;%s;%s;%s ($SERVICESTATE$);%s;$SERVICEOUTPUT$;$NOTIFICATIONAUTHOR$;$NOTIFICATIONCOMMENT$\n", cntct->name, svc->host_name, svc->description, notification_reason_name(type), command_name_ptr);
			} else {
				nm_asprintf(&temp_buffer, "SERVICE NOTIFICATION: %s;%s;%s;$SERVICESTATE$;%s;$SERVICEOUTPUT$\n", cntct->name, svc->host_name, svc->description, command_name_ptr);
			}
			process_macros_r(mac, temp_buffer, &processed_buffer, 0);
			nm_log(NSLOG_SERVICE_NOTIFICATION, "%s", processed_buffer);

			nm_free(temp_buffer);
			nm_free(processed_buffer);
		}

		/* run the notification command */
		nj = nm_calloc(1, sizeof(struct notification_job));
		nj->ctc = cntct;
		nj->hst = svc->host_ptr;
		nj->svc = svc;
		if (ERROR == wproc_run_callback(processed_command, notification_timeout, notification_handle_job_result, nj, mac)) {
			nm_log(NSLOG_RUNTIME_ERROR, "wproc: Unable to send notification for service '%s on host '%s' to worker\n", svc->description, svc->host_ptr->name);
			free(nj);
		}

		nm_free(command_name);
		nm_free(processed_command);

		/* get end time */
		gettimeofday(&method_end_time, NULL);

		broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_END, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_NOTIFICATION, type, method_start_time, method_end_time, (void *)svc, cntct, temp_commandsmember->command, not_author, not_data, escalated);
	}

	/* get end time */
	gettimeofday(&end_time, NULL);

	/* update the contact's last service notification time */
	cntct->last_service_notification = start_time.tv_sec;

	broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_END, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_NOTIFICATION, type, start_time, end_time, (void *)svc, cntct, not_author, not_data, escalated);

	return OK;
}


/* checks to see if a service escalation entry is a match for the current service notification */
int is_valid_escalation_for_service_notification(service *svc, serviceescalation *se, int options)
{
	int notification_number = 0;
	time_t current_time = 0L;
	service *temp_service = NULL;

	/* get the current time */
	time(&current_time);

	/* if this is a recovery, really we check for who got notified about a previous problem */
	if (svc->current_state == STATE_OK)
		notification_number = svc->current_notification_number - 1;
	else
		notification_number = svc->current_notification_number;

	/* this entry if it is not for this service */
	temp_service = se->service_ptr;
	if (temp_service == NULL || temp_service != svc)
		return FALSE;

	/*** EXCEPTION ***/
	/* broadcast options go to everyone, so this escalation is valid */
	if (options & NOTIFICATION_OPTION_BROADCAST)
		return TRUE;

	/* skip this escalation if it happens later */
	if (se->first_notification > notification_number)
		return FALSE;

	/* skip this escalation if it has already passed */
	if (se->last_notification != 0 && se->last_notification < notification_number)
		return FALSE;

	/* skip this escalation if the state options don't match */
	if (flag_isset(se->escalation_options, 1 << svc->current_state) == FALSE)
		return FALSE;

	/* skip this escalation if it has a timeperiod and the current time isn't valid */
	if (se->escalation_period != NULL && check_time_against_period(current_time, se->escalation_period_ptr) == ERROR)
		return FALSE;

	return TRUE;
}


/* checks to see whether a service notification should be escalation */
int should_service_notification_be_escalated(service *svc)
{
	objectlist *list;

	/* search the service escalation list */
	for (list = svc->escalation_list; list; list = list->next) {
		serviceescalation *temp_se = (serviceescalation *)list->object_ptr;

		/* we found a matching entry, so escalate this notification! */
		if (is_valid_escalation_for_service_notification(svc, temp_se, NOTIFICATION_OPTION_NONE) == TRUE) {
			log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Service notification WILL be escalated.\n");
			return TRUE;
		}
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Service notification will NOT be escalated.\n");

	return FALSE;
}


/* given a service, create a list of contacts to be notified, removing duplicates, checking contact notification viability */
static notification *create_notification_list_from_service(nagios_macros *mac, service *svc, int options, int *escalated, int type)
{
	notification *notification_list = NULL;
	serviceescalation *temp_se = NULL;
	contactsmember *temp_contactsmember = NULL;
	contact *temp_contact = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;
	int escalate_notification = FALSE;

	/* see if this notification should be escalated */
	escalate_notification = should_service_notification_be_escalated(svc);

	/* set the escalation flag */
	*escalated = escalate_notification;

	/* set the escalation macro */
	mac->x[MACRO_NOTIFICATIONISESCALATED] = nm_strdup(escalate_notification ? "1" : "0");

	if (options & NOTIFICATION_OPTION_BROADCAST)
		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "This notification will be BROADCAST to all (escalated and normal) contacts...\n");

	/* use escalated contacts for this notification */
	if (escalate_notification == TRUE || (options & NOTIFICATION_OPTION_BROADCAST)) {
		objectlist *list;

		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Adding contacts from service escalation(s) to notification list.\n");

		/* search all the escalation entries for valid matches */
		for (list = svc->escalation_list; list; list = list->next) {
			temp_se = (serviceescalation *)list->object_ptr;

			/* skip this entry if it isn't appropriate */
			if (is_valid_escalation_for_service_notification(svc, temp_se, options) == FALSE)
				continue;

			log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding individual contacts from service escalation(s) to notification list.\n");

			/* add all individual contacts for this escalation entry */
			for (temp_contactsmember = temp_se->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
				if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
					continue;
				/* check now if the contact can be notified */
				if (check_contact_service_notification_viability(temp_contact, svc, type, options) == OK)
					add_notification(&notification_list, mac, temp_contact);
				else
					log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
			}

			log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding members of contact groups from service escalation(s) to notification list.\n");

			/* add all contacts that belong to contactgroups for this escalation */
			for (temp_contactgroupsmember = temp_se->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
				log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding members of contact group '%s' for service escalation to notification list.\n", temp_contactgroupsmember->group_name);
				if ((temp_contactgroup = temp_contactgroupsmember->group_ptr) == NULL)
					continue;
				for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
					if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
						continue;
					/* check now if the contact can be notified */
					if (check_contact_service_notification_viability(temp_contact, svc, type, options) == OK)
						add_notification(&notification_list, mac, temp_contact);
					else
						log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
				}
			}
		}
	}

	/* else use normal, non-escalated contacts */
	if (escalate_notification == FALSE || (options & NOTIFICATION_OPTION_BROADCAST)) {

		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Adding normal contacts for service to notification list.\n");

		/* add all individual contacts for this service */
		for (temp_contactsmember = svc->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
			if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
				continue;
			/* check now if the contact can be notified */
			if (check_contact_service_notification_viability(temp_contact, svc, type, options) == OK)
				add_notification(&notification_list, mac, temp_contact);
			else
				log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
		}

		/* add all contacts that belong to contactgroups for this service */
		for (temp_contactgroupsmember = svc->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
			log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding members of contact group '%s' for service to notification list.\n", temp_contactgroupsmember->group_name);
			if ((temp_contactgroup = temp_contactgroupsmember->group_ptr) == NULL)
				continue;
			for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
				if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
					continue;
				/* check now if the contact can be notified */
				if (check_contact_service_notification_viability(temp_contact, svc, type, options) == OK)
					add_notification(&notification_list, mac, temp_contact);
				else
					log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
			}
		}
	}

	return notification_list;
}


/******************************************************************/
/******************* HOST NOTIFICATION FUNCTIONS ******************/
/******************************************************************/


/* notify all contacts for a host that the entire host is down or up */
int host_notification(host *hst, int type, char *not_author, char *not_data, int options)
{
	notification *notification_list = NULL;
	notification *temp_notification = NULL;
	contact *temp_contact = NULL;
	time_t current_time;
	struct timeval start_time;
	struct timeval end_time;
	int escalated = FALSE;
	int result = OK;
	int contacts_notified = 0;
	int increment_notification_number = FALSE;
	nagios_macros mac;
	neb_cb_resultset *neb_resultset = NULL;
	neb_cb_resultset_iter neb_resultset_iter;
	neb_cb_result *neb_result = NULL;

	/* get the current time */
	time(&current_time);
	gettimeofday(&start_time, NULL);

	log_debug_info(DEBUGL_NOTIFICATIONS, 0, "** Host Notification Attempt ** Host: '%s', Type: %s, Options: %d, Current State: %d, Last Notification: %s\n", hst->name, notification_reason_name(type), options, hst->current_state, ctime(&hst->last_notification));

	/* check viability of sending out a host notification */
	if (check_host_notification_viability(hst, type, options) == ERROR) {
		log_debug_info(DEBUGL_NOTIFICATIONS, 0, "Notification viability test failed.  No notification will be sent out.\n");
		return OK;
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 0, "Notification viability test passed.\n");

	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_resultset = broker_notification_data(NEBTYPE_NOTIFICATION_START, NEBFLAG_NONE, NEBATTR_NONE, HOST_NOTIFICATION, type, start_time, end_time, (void *)hst, not_author, not_data, escalated, 0);
	neb_cb_resultset_iter_init(&neb_resultset_iter, neb_resultset);
	while (neb_cb_resultset_iter_next(&neb_resultset_iter, &neb_result)) {
		int rc = neb_cb_result_returncode(neb_result);

		if (rc == NEBERROR_CALLBACKCANCEL || rc == NEBERROR_CALLBACKOVERRIDE) {
			char *description = NULL;
			nm_asprintf(&description, "Module '%s' %s: '%s'",
			            neb_cb_result_module_name(neb_result),
			            rc == NEBERROR_CALLBACKCANCEL ? "cancelled notification" : "overrode notification",
			            neb_cb_result_description(neb_result));
			LOG_HOST_NSR_NEB_BLOCKED(NSR_NEB_BLOCKED, description);
			nm_free(description);
			neb_cb_resultset_destroy(neb_resultset);
			return rc == NEBERROR_CALLBACKCANCEL ? ERROR : OK;
		}
	}
	neb_cb_resultset_destroy(neb_resultset);

	/* should the notification number be increased? */
	if (type == NOTIFICATION_NORMAL || (options & NOTIFICATION_OPTION_INCREMENT)) {
		hst->current_notification_number++;
		increment_notification_number = TRUE;
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Current notification number: %d (%s)\n", hst->current_notification_number, (increment_notification_number == TRUE) ? "incremented" : "unchanged");

	/* save and increase the current notification id */
	nm_free(hst->current_notification_id);
	hst->current_notification_id = g_uuid_string_random();

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Creating list of contacts to be notified.\n");

	/* reset memory for local macro data */
	memset(&mac, 0, sizeof(mac));

	/*
	 * check viability before adding a contact to the notification
	 * list and build up the $NOTIFICATIONRECIPIENTS$ macro while
	 * we're at it.
	 * This prevents us from running through all the steps again in
	 * notify_contact_of_host|service.
	 * Furthermore the $NOTIFICATIONRECIPIENTS$ macro will contain
	 * only actual recipients (as the name implies), and not all
	 * contacts assigned to that host|service.
	 *
	 * note: checks against timeperiod will happen now(),
	 * and not when the notification is actually being sent.
	 */
	notification_list = create_notification_list_from_host(&mac, hst, options, &escalated, type);

	/* there are contacts to be notified... */
	if (notification_list != NULL) {

		/* grab the macro variables */
		grab_host_macros_r(&mac, hst);

		/* if this notification has an author, attempt to lookup the associated contact */
		if (not_author != NULL) {
			temp_contact = find_contact_by_name_or_alias(not_author);
		}

		/* get author and comment macros */
		if (not_author)
			mac.x[MACRO_NOTIFICATIONAUTHOR] = nm_strdup(not_author);
		if (temp_contact != NULL) {
			mac.x[MACRO_NOTIFICATIONAUTHORNAME] = nm_strdup(temp_contact->name);
			mac.x[MACRO_NOTIFICATIONAUTHORALIAS] = nm_strdup(temp_contact->alias);
		}
		if (not_data)
			mac.x[MACRO_NOTIFICATIONCOMMENT] = nm_strdup(not_data);

		/* NOTE: these macros are deprecated and will likely disappear in Nagios 4.x */
		/* if this is an acknowledgement, get author and comment macros */
		if (type == NOTIFICATION_ACKNOWLEDGEMENT) {

			if (not_author)
				mac.x[MACRO_HOSTACKAUTHOR] = nm_strdup(not_author);

			if (not_data)
				mac.x[MACRO_HOSTACKCOMMENT] = nm_strdup(not_data);

			if (temp_contact != NULL) {
				mac.x[MACRO_HOSTACKAUTHORNAME] = nm_strdup(temp_contact->name);
				mac.x[MACRO_HOSTACKAUTHORALIAS] = nm_strdup(temp_contact->alias);
			}
		}

		/* set the notification type macro */
		if (type != NOTIFICATION_NORMAL) {
			mac.x[MACRO_NOTIFICATIONTYPE] = nm_strdup(notification_reason_name(type));
		} else if (hst->current_state == STATE_UP) {
			mac.x[MACRO_NOTIFICATIONTYPE] = nm_strdup("RECOVERY");
		} else {
			mac.x[MACRO_NOTIFICATIONTYPE] = nm_strdup("PROBLEM");
		}


		/* set the notification number macro */
		nm_asprintf(&mac.x[MACRO_HOSTNOTIFICATIONNUMBER], "%d", hst->current_notification_number);

		/* the $NOTIFICATIONNUMBER$ macro is maintained for backward compatibility */
		mac.x[MACRO_NOTIFICATIONNUMBER] = nm_strdup(mac.x[MACRO_HOSTNOTIFICATIONNUMBER]);

		/* set the notification id macro */
		nm_asprintf(&mac.x[MACRO_HOSTNOTIFICATIONID], "%s", hst->current_notification_id);

		/* notify each contact (duplicates have been removed) */
		for (temp_notification = notification_list; temp_notification != NULL; temp_notification = temp_notification->next) {

			/* grab the macro variables for this contact */
			grab_contact_macros_r(&mac, temp_notification->contact);

			/* clear summary macros (they are customized for each contact) */
			clear_summary_macros_r(&mac);

			/* notify this contact */
			result = notify_contact_of_host(&mac, temp_notification->contact, hst, type, not_author, not_data, options, escalated);

			/* keep track of how many contacts were notified */
			if (result == OK)
				contacts_notified++;
		}

		free_notification_list(notification_list);

		/* clear out all macros we created */
		nm_free(mac.x[MACRO_HOSTNOTIFICATIONID]);
		nm_free(mac.x[MACRO_NOTIFICATIONNUMBER]);
		nm_free(mac.x[MACRO_NOTIFICATIONCOMMENT]);
		nm_free(mac.x[MACRO_HOSTNOTIFICATIONNUMBER]);
		nm_free(mac.x[MACRO_NOTIFICATIONTYPE]);
		nm_free(mac.x[MACRO_NOTIFICATIONAUTHOR]);
		nm_free(mac.x[MACRO_NOTIFICATIONAUTHORNAME]);
		nm_free(mac.x[MACRO_NOTIFICATIONAUTHORALIAS]);
		nm_free(mac.x[MACRO_HOSTACKAUTHORNAME]);
		nm_free(mac.x[MACRO_HOSTACKAUTHORALIAS]);
		nm_free(mac.x[MACRO_HOSTACKAUTHOR]);
		nm_free(mac.x[MACRO_HOSTACKCOMMENT]);
		/* this gets set in add_notification() */
		nm_free(mac.x[MACRO_NOTIFICATIONRECIPIENTS]);

		/*
		 * Clear all macros, or they will linger in memory
		 * now that we're done with the notifications.
		 */
		clear_summary_macros_r(&mac);
		clear_contact_macros_r(&mac);
		clear_argv_macros_r(&mac);
		clear_host_macros_r(&mac);

		if (type == NOTIFICATION_NORMAL) {

			/* adjust last/next notification time and notification flags if we notified someone */
			if (contacts_notified > 0) {

				/* calculate the next acceptable re-notification time */
				hst->next_notification = get_next_host_notification_time(hst, current_time);

				/* update the last notification time for this host (this is needed for scheduling the next problem notification) */
				hst->last_notification = current_time;

				/* update notifications flags */
				add_notified_on(hst, hst->current_state);

				log_debug_info(DEBUGL_NOTIFICATIONS, 0, "%d contacts were notified.  Next possible notification time: %s\n", contacts_notified, ctime(&hst->next_notification));
				hst->last_update = current_time;
			}

			/* we didn't end up notifying anyone */
			else if (increment_notification_number == TRUE) {

				/* adjust current notification number */
				hst->current_notification_number--;

				log_debug_info(DEBUGL_NOTIFICATIONS, 0, "No contacts were notified.  Next possible notification time: %s\n", ctime(&hst->next_notification));
				hst->last_update = current_time;
			}
		}

		log_debug_info(DEBUGL_NOTIFICATIONS, 0, "%d contacts were notified.\n", contacts_notified);
	}

	/* there were no contacts, so no notification really occurred... */
	else {

		/* adjust notification number, since no notification actually went out */
		if (increment_notification_number == TRUE)
			hst->current_notification_number--;
		LOG_HOST_NSR(NSR_NO_CONTACTS);
	}

	/* this gets set in create_notification_list_from_host() */
	nm_free(mac.x[MACRO_NOTIFICATIONISESCALATED]);

	/* get the time we finished */
	gettimeofday(&end_time, NULL);

	neb_resultset = broker_notification_data(NEBTYPE_NOTIFICATION_END, NEBFLAG_NONE, NEBATTR_NONE, HOST_NOTIFICATION, type, start_time, end_time, (void *)hst, not_author, not_data, escalated, contacts_notified);
	neb_cb_resultset_destroy(neb_resultset);

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return OK;
}


/* checks viability of sending a host notification */
int check_host_notification_viability(host *hst, int type, int options)
{
	time_t current_time;
	time_t timeperiod_start;
	time_t first_problem_time;

	/* forced notifications bust through everything */
	if (options & NOTIFICATION_OPTION_FORCED) {
		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "This is a forced host notification, so we'll send it out.\n");
		return OK;
	}

	/* get current time */
	time(&current_time);

	/* are notifications enabled? */
	if (enable_notifications == FALSE) {
		LOG_HOST_NSR(NSR_DISABLED);
		return ERROR;
	}

	/* see if the host can have notifications sent out at this time */
	if (check_time_against_period(current_time, hst->notification_period_ptr) == ERROR) {

		LOG_HOST_NSR(NSR_TIMEPERIOD_BLOCKED);
		/* if this is a normal notification, calculate the next acceptable notification time, once the next valid time range arrives... */
		if (type == NOTIFICATION_NORMAL) {

			get_next_valid_time(current_time, &timeperiod_start, hst->notification_period_ptr);

			/* it looks like there is no notification time defined, so schedule next one far into the future (one year)... */
			if (timeperiod_start == (time_t)0)
				hst->next_notification = (time_t)(current_time + (60 * 60 * 24 * 365));

			/* else use the next valid notification time */
			else
				hst->next_notification = timeperiod_start;

			log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Next possible notification time: %s\n", ctime(&hst->next_notification));
		}

		return ERROR;
	}

	/* are notifications temporarily disabled for this host? */
	if (hst->notifications_enabled == FALSE) {
		LOG_HOST_NSR(NSR_DISABLED_OBJECT);
		return ERROR;
	}


	/*********************************************/
	/*** SPECIAL CASE FOR CUSTOM NOTIFICATIONS ***/
	/*********************************************/

	/* custom notifications are good to go at this point... */
	if (type == NOTIFICATION_CUSTOM) {
		if (hst->scheduled_downtime_depth > 0) {
			LOG_HOST_NSR(NSR_CUSTOM_SCHED_DOWNTIME);
			return ERROR;
		}
		return OK;
	}



	/****************************************/
	/*** SPECIAL CASE FOR ACKNOWLEGEMENTS ***/
	/****************************************/

	/* acknowledgements only have to pass three general filters, although they have another test of their own... */
	if (type == NOTIFICATION_ACKNOWLEDGEMENT) {

		/* don't send an acknowledgement if there isn't a problem... */
		if (hst->current_state == STATE_UP) {
			LOG_HOST_NSR(NSR_ACK_OBJECT_OK);
			return ERROR;
		}

		/* acknowledgement viability test passed, so the notification can be sent out */
		return OK;
	}


	/*****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/*****************************************/

	/* flapping notifications only have to pass three general filters */
	if (type == NOTIFICATION_FLAPPINGSTART || type == NOTIFICATION_FLAPPINGSTOP || type == NOTIFICATION_FLAPPINGDISABLED) {

		/* don't send a notification if we're not supposed to... */
		if (!(hst->notification_options & OPT_FLAPPING)) {
			LOG_HOST_NSR(NSR_NO_FLAPPING);
			return ERROR;
		}

		/* don't send notifications during scheduled downtime */
		if (hst->scheduled_downtime_depth > 0) {
			LOG_HOST_NSR(NSR_SCHED_DOWNTIME_FLAPPING);
			return ERROR;
		}

		/* flapping viability test passed, so the notification can be sent out */
		return OK;
	}


	/*****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/*****************************************/

	/* flapping notifications only have to pass three general filters */
	if (type == NOTIFICATION_DOWNTIMESTART || type == NOTIFICATION_DOWNTIMEEND || type == NOTIFICATION_DOWNTIMECANCELLED) {

		/* don't send a notification if we're not supposed to... */
		if ((hst->notification_options & OPT_DOWNTIME) == FALSE) {
			LOG_HOST_NSR(NSR_NO_DOWNTIME);
			return ERROR;
		}

		/* don't send notifications during scheduled downtime */
		if (hst->scheduled_downtime_depth > 0) {
			LOG_HOST_NSR(NSR_SCHED_DOWNTIME_DOWNTIME);
			return ERROR;
		}

		/* downtime viability test passed, so the notification can be sent out */
		return OK;
	}


	/****************************************/
	/*** NORMAL NOTIFICATIONS ***************/
	/****************************************/

	/* is this a hard problem/recovery? */
	if (hst->state_type == SOFT_STATE) {
		LOG_HOST_NSR(NSR_SOFT_STATE);
		return ERROR;
	}

	/* has this problem already been acknowledged? */
	if (hst->problem_has_been_acknowledged == TRUE) {
		LOG_HOST_NSR(NSR_ACKNOWLEDGED);
		return ERROR;
	}

	/* check notification dependencies */
	if (check_host_dependencies(hst, NOTIFICATION_DEPENDENCY) == DEPENDENCIES_FAILED) {
		LOG_HOST_NSR(NSR_DEPENDENCY_FAILURE);
		return ERROR;
	}

	/* see if we should notify about problems with this host */
	if ((hst->notification_options & (1 << hst->current_state)) == FALSE) {
		LOG_HOST_NSR(NSR_STATE_DISABLED);
		return ERROR;
	}
	if (hst->current_state == STATE_UP) {

		if ((hst->notification_options & OPT_RECOVERY) == FALSE) {
			LOG_HOST_NSR(NSR_NO_RECOVERY);
			return ERROR;
		}
		if (!hst->notified_on) {
			LOG_HOST_NSR(NSR_RECOVERY_UNNOTIFIED_PROBLEM);
			return ERROR;
		}

	}

	/* see if enough time has elapsed for first notification (Mathias Sundman) */
	/* 10/02/07 don't place restrictions on recoveries or non-normal notifications, must use last time up (or program start) in calculation */
	/* it is reasonable to assume that if the host was never up, the program start time should be used in this calculation */
	if (type == NOTIFICATION_NORMAL
	    && hst->current_notification_number == 0
	    && hst->first_notification_delay > 0
	    && hst->current_state != STATE_UP) {

		first_problem_time = hst->last_hard_state_change > 0 ? hst->last_hard_state_change : program_start;

		if (current_time < first_problem_time + (time_t)(hst->first_notification_delay * interval_length)) {
			LOG_HOST_NSR(NSR_DELAY);
			return ERROR;
		}
	}

	/* if this host is currently flapping, don't send the notification */
	if (enable_flap_detection == TRUE && hst->flap_detection_enabled == TRUE && hst->is_flapping == TRUE) {
		LOG_HOST_NSR(NSR_IS_FLAPPING);
		return ERROR;
	}

	/***** RECOVERY NOTIFICATIONS ARE GOOD TO GO AT THIS POINT *****/
	if (hst->current_state == STATE_UP)
		return OK;

	/* if this host is currently in a scheduled downtime period, don't send the notification */
	if (hst->scheduled_downtime_depth > 0) {
		LOG_HOST_NSR(NSR_IS_SCHEDULED_DOWNTIME);
		return ERROR;
	}

	/* check if we shouldn't renotify contacts about the host problem */
	if (hst->no_more_notifications == TRUE) {
		LOG_HOST_NSR(NSR_RE_NO_MORE);
		return ERROR;
	}

	/* check if its time to re-notify the contacts about the host... */
	if (current_time < hst->next_notification) {
		LOG_HOST_NSR(NSR_RE_NOT_YET);
		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Next acceptable notification time: %s\n", ctime(&hst->next_notification));
		return ERROR;
	}

	return OK;
}


/* checks the viability of notifying a specific contact about a host */
int check_contact_host_notification_viability(contact *cntct, host *hst, int type, int options)
{

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "** Checking host notification viability for contact '%s'...\n", cntct->name);

	/* forced notifications bust through everything */
	if (options & NOTIFICATION_OPTION_FORCED) {
		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "This is a forced host notification, so we'll send it out for this contact.\n");
		return OK;
	}

	/* are notifications enabled? */
	if (cntct->host_notifications_enabled == FALSE) {
		LOG_HOST_CONTACT_NSR(NSR_DISABLED_OBJECT);
		return ERROR;
	}

	/* is this host important enough? */
	if (cntct->minimum_value > hst->hourly_value && cntct->minimum_value > hst->hourly_value + host_services_value(hst)) {
		LOG_HOST_CONTACT_NSR(NSR_INSUFF_IMPORTANCE);
		return ERROR;
	}

	/* see if the contact can be notified at this time */
	if (check_time_against_period(time(NULL), cntct->host_notification_period_ptr) == ERROR) {
		LOG_HOST_CONTACT_NSR(NSR_TIMEPERIOD_BLOCKED);
		return ERROR;
	}


	/*********************************************/
	/*** SPECIAL CASE FOR CUSTOM NOTIFICATIONS ***/
	/*********************************************/

	/* custom notifications are good to go at this point... */
	if (type == NOTIFICATION_CUSTOM)
		return OK;


	/****************************************/
	/*** SPECIAL CASE FOR FLAPPING ALERTS ***/
	/****************************************/

	if (type == NOTIFICATION_FLAPPINGSTART || type == NOTIFICATION_FLAPPINGSTOP || type == NOTIFICATION_FLAPPINGDISABLED) {

		if ((cntct->host_notification_options & OPT_FLAPPING) == FALSE) {
			LOG_HOST_CONTACT_NSR(NSR_NO_FLAPPING);
			return ERROR;
		}

		return OK;
	}


	/****************************************/
	/*** SPECIAL CASE FOR DOWNTIME ALERTS ***/
	/****************************************/

	if (type == NOTIFICATION_DOWNTIMESTART || type == NOTIFICATION_DOWNTIMEEND || type == NOTIFICATION_DOWNTIMECANCELLED) {

		if (flag_isset(cntct->host_notification_options, OPT_DOWNTIME) == FALSE) {
			LOG_HOST_CONTACT_NSR(NSR_NO_DOWNTIME);
			return ERROR;
		}

		return OK;
	}


	/*************************************/
	/*** ACKS AND NORMAL NOTIFICATIONS ***/
	/*************************************/

	/* see if we should notify about problems with this host */
	if (flag_isset(cntct->host_notification_options, 1 << hst->current_state) == FALSE) {
		LOG_HOST_CONTACT_NSR(NSR_STATE_DISABLED);
		return ERROR;
	}

	if (hst->current_state == STATE_UP && hst->notified_on == 0) {
		LOG_HOST_CONTACT_NSR(NSR_RECOVERY_UNNOTIFIED_PROBLEM);
		return ERROR;
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "** Host notification viability for contact '%s' PASSED.\n", cntct->name);

	return OK;
}


/* notify a specific contact that an entire host is down or up */
int notify_contact_of_host(nagios_macros *mac, contact *cntct, host *hst, int type, char *not_author, char *not_data, int options, int escalated)
{
	commandsmember *temp_commandsmember = NULL;
	char *command_name = NULL;
	char *command_name_ptr = NULL;
	char *temp_buffer = NULL;
	char *processed_buffer = NULL;
	char *raw_command = NULL;
	char *processed_command = NULL;
	struct timeval start_time;
	struct timeval end_time;
	struct timeval method_start_time;
	struct timeval method_end_time;
	int macro_options = STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
	int neb_result;
	struct notification_job *nj;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "** Notifying contact '%s'\n", cntct->name);

	/* get start time */
	gettimeofday(&start_time, NULL);

	end_time.tv_sec = 0L;
	end_time.tv_usec = 0L;
	neb_result = broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_START, NEBFLAG_NONE, NEBATTR_NONE, HOST_NOTIFICATION, type, start_time, end_time, (void *)hst, cntct, not_author, not_data, escalated);
	if (NEBERROR_CALLBACKCANCEL == neb_result)
		return ERROR;
	else if (NEBERROR_CALLBACKOVERRIDE == neb_result)
		return OK;

	/* process all the notification commands this user has */
	for (temp_commandsmember = cntct->host_notification_commands; temp_commandsmember != NULL; temp_commandsmember = temp_commandsmember->next) {

		/* get start time */
		gettimeofday(&method_start_time, NULL);

		method_end_time.tv_sec = 0L;
		method_end_time.tv_usec = 0L;
		neb_result = broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_START, NEBFLAG_NONE, NEBATTR_NONE, HOST_NOTIFICATION, type, method_start_time, method_end_time, (void *)hst, cntct, temp_commandsmember->command, not_author, not_data, escalated);
		if (NEBERROR_CALLBACKCANCEL == neb_result)
			break ;
		else if (NEBERROR_CALLBACKOVERRIDE == neb_result)
			continue ;

		get_raw_command_line_r(mac, temp_commandsmember->command_ptr, temp_commandsmember->command, &raw_command, macro_options);
		if (raw_command == NULL)
			continue;

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Raw notification command: %s\n", raw_command);

		/* process any macros contained in the argument */
		process_macros_r(mac, raw_command, &processed_command, macro_options);
		nm_free(raw_command);
		if (processed_command == NULL)
			continue;

		/* get the command name */
		command_name = nm_strdup(temp_commandsmember->command);
		command_name_ptr = strtok(command_name, "!");

		/* run the notification command... */

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Processed notification command: %s\n", processed_command);

		/* log the notification to program log file */
		if (log_notifications == TRUE) {
			if (type != NOTIFICATION_NORMAL) {
				nm_asprintf(&temp_buffer, "HOST NOTIFICATION: %s;%s;%s ($HOSTSTATE$);%s;$HOSTOUTPUT$;$NOTIFICATIONAUTHOR$;$NOTIFICATIONCOMMENT$\n", cntct->name, hst->name, notification_reason_name(type), command_name_ptr);
			} else {
				nm_asprintf(&temp_buffer, "HOST NOTIFICATION: %s;%s;$HOSTSTATE$;%s;$HOSTOUTPUT$\n", cntct->name, hst->name, command_name_ptr);
			}
			process_macros_r(mac, temp_buffer, &processed_buffer, 0);
			nm_log(NSLOG_HOST_NOTIFICATION, "%s", processed_buffer);

			nm_free(temp_buffer);
			nm_free(processed_buffer);
		}

		/* run the notification command */
		nj = nm_calloc(1, sizeof(struct notification_job));
		nj->ctc = cntct;
		nj->hst = hst;
		nj->svc = NULL;
		if (ERROR == wproc_run_callback(processed_command, notification_timeout, notification_handle_job_result, nj, mac)) {
			nm_log(NSLOG_RUNTIME_ERROR, "wproc: Unable to send notification for host '%s' to worker\n", hst->name);
			free(nj);
		}

		/* @todo Handle nebmod stuff when getting results from workers */

		nm_free(command_name);
		nm_free(processed_command);

		/* get end time */
		gettimeofday(&method_end_time, NULL);

		broker_contact_notification_method_data(NEBTYPE_CONTACTNOTIFICATIONMETHOD_END, NEBFLAG_NONE, NEBATTR_NONE, HOST_NOTIFICATION, type, method_start_time, method_end_time, (void *)hst, cntct, temp_commandsmember->command, not_author, not_data, escalated);
	}

	/* get end time */
	gettimeofday(&end_time, NULL);

	/* update the contact's last host notification time */
	cntct->last_host_notification = start_time.tv_sec;

	broker_contact_notification_data(NEBTYPE_CONTACTNOTIFICATION_END, NEBFLAG_NONE, NEBATTR_NONE, HOST_NOTIFICATION, type, start_time, end_time, (void *)hst, cntct, not_author, not_data, escalated);

	return OK;
}


/* checks to see if a host escalation entry is a match for the current host notification */
int is_valid_escalation_for_host_notification(host *hst, hostescalation *he, int options)
{
	int notification_number = 0;
	time_t current_time = 0L;
	host *temp_host = NULL;

	/* get the current time */
	time(&current_time);

	/* if this is a recovery, really we check for who got notified about a previous problem */
	if (hst->current_state == STATE_UP)
		notification_number = hst->current_notification_number - 1;
	else
		notification_number = hst->current_notification_number;

	/* find the host this escalation entry is associated with */
	temp_host = he->host_ptr;
	if (temp_host == NULL || temp_host != hst)
		return FALSE;

	/*** EXCEPTION ***/
	/* broadcast options go to everyone, so this escalation is valid */
	if (options & NOTIFICATION_OPTION_BROADCAST)
		return TRUE;

	/* skip this escalation if it happens later */
	if (he->first_notification > notification_number)
		return FALSE;

	/* skip this escalation if it has already passed */
	if (he->last_notification != 0 && he->last_notification < notification_number)
		return FALSE;

	/* skip this escalation if the state options don't match */
	if (flag_isset(he->escalation_options, 1 << hst->current_state) == FALSE)
		return FALSE;

	/* skip this escalation if it has a timeperiod and the current time isn't valid */
	if (he->escalation_period != NULL && check_time_against_period(current_time, he->escalation_period_ptr) == ERROR)
		return FALSE;

	return TRUE;
}


/* checks to see whether a host notification should be escalation */
int should_host_notification_be_escalated(host *hst)
{
	objectlist *list;

	if (hst == NULL)
		return FALSE;

	/* search the host escalation list */
	for (list = hst->escalation_list; list; list = list->next) {
		hostescalation *temp_he = (hostescalation *)list->object_ptr;
		/* we found a matching entry, so escalate this notification! */
		if (is_valid_escalation_for_host_notification(hst, temp_he, NOTIFICATION_OPTION_NONE) == TRUE)
			return TRUE;
	}

	log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Host notification will NOT be escalated.\n");

	return FALSE;
}


/* given a host, create a list of contacts to be notified, removing duplicates, checking contact notification viability */
static notification *create_notification_list_from_host(nagios_macros *mac, host *hst, int options, int *escalated, int type)
{
	notification *notification_list = NULL;
	hostescalation *temp_he = NULL;
	contactsmember *temp_contactsmember = NULL;
	contact *temp_contact = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;
	int escalate_notification = FALSE;

	/* see if this notification should be escalated */
	escalate_notification = should_host_notification_be_escalated(hst);

	/* set the escalation flag */
	*escalated = escalate_notification;

	/* set the escalation macro */
	mac->x[MACRO_NOTIFICATIONISESCALATED] = nm_strdup(escalate_notification ? "1" : "0");

	if (options & NOTIFICATION_OPTION_BROADCAST)
		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "This notification will be BROADCAST to all (escalated and normal) contacts...\n");

	/* use escalated contacts for this notification */
	if (escalate_notification == TRUE || (options & NOTIFICATION_OPTION_BROADCAST)) {
		objectlist *list;

		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Adding contacts from host escalation(s) to notification list.\n");

		/* check all the host escalation entries */
		for (list = hst->escalation_list; list; list = list->next) {
			temp_he = (hostescalation *)list->object_ptr;

			/* see if this escalation if valid for this notification */
			if (is_valid_escalation_for_host_notification(hst, temp_he, options) == FALSE)
				continue;

			log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding individual contacts from host escalation(s) to notification list.\n");

			/* add all individual contacts for this escalation */
			for (temp_contactsmember = temp_he->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
				if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
					continue;
				/* check now if the contact can be notified */
				if (check_contact_host_notification_viability(temp_contact, hst, type, options) == OK)
					add_notification(&notification_list, mac, temp_contact);
				else
					log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
			}

			log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding members of contact groups from host escalation(s) to notification list.\n");

			/* add all contacts that belong to contactgroups for this escalation */
			for (temp_contactgroupsmember = temp_he->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
				log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding members of contact group '%s' for host escalation to notification list.\n", temp_contactgroupsmember->group_name);
				if ((temp_contactgroup = temp_contactgroupsmember->group_ptr) == NULL)
					continue;
				for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
					if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
						continue;
					/* check now if the contact can be notified */
					if (check_contact_host_notification_viability(temp_contact, hst, type, options) == OK)
						add_notification(&notification_list, mac, temp_contact);
					else
						log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
				}
			}
		}
	}

	/* use normal, non-escalated contacts for this notification */
	if (escalate_notification == FALSE  || (options & NOTIFICATION_OPTION_BROADCAST)) {

		log_debug_info(DEBUGL_NOTIFICATIONS, 1, "Adding normal contacts for host to notification list.\n");

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding individual contacts for host to notification list.\n");

		/* add all individual contacts for this host */
		for (temp_contactsmember = hst->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
			if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
				continue;
			/* check now if the contact can be notified */
			if (check_contact_host_notification_viability(temp_contact, hst, type, options) == OK)
				add_notification(&notification_list, mac, temp_contact);
			else
				log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
		}

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding members of contact groups for host to notification list.\n");

		/* add all contacts that belong to contactgroups for this host */
		for (temp_contactgroupsmember = hst->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
			log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding members of contact group '%s' for host to notification list.\n", temp_contactgroupsmember->group_name);

			if ((temp_contactgroup = temp_contactgroupsmember->group_ptr) == NULL)
				continue;
			for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
				if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
					continue;
				/* check now if the contact can be notified */
				if (check_contact_host_notification_viability(temp_contact, hst, type, options) == OK)
					add_notification(&notification_list, mac, temp_contact);
				else
					log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Not adding contact '%s'\n", temp_contact->name);
			}
		}
	}

	return notification_list;
}


/******************************************************************/
/***************** NOTIFICATION TIMING FUNCTIONS ******************/
/******************************************************************/

/* calculates next acceptable re-notification time for a service */
time_t get_next_service_notification_time(service *svc, time_t offset)
{
	time_t next_notification = 0L;
	double interval_to_use = 0.0;
	objectlist *list;
	int have_escalated_interval = FALSE;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Calculating next valid notification time...\n");

	/* default notification interval */
	interval_to_use = svc->notification_interval;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Default interval: %f\n", interval_to_use);

	/* search all the escalation entries for valid matches for this service (at its current notification number) */
	for (list = svc->escalation_list; list; list = list->next) {
		serviceescalation *temp_se = (serviceescalation *)list->object_ptr;

		/* interval < 0 means to use non-escalated interval */
		if (temp_se->notification_interval < 0.0)
			continue;

		/* skip this entry if it isn't appropriate */
		if (is_valid_escalation_for_service_notification(svc, temp_se, NOTIFICATION_OPTION_NONE) == FALSE)
			continue;

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Found a valid escalation w/ interval of %f\n", temp_se->notification_interval);

		/* if we haven't used a notification interval from an escalation yet, use this one */
		if (have_escalated_interval == FALSE) {
			have_escalated_interval = TRUE;
			interval_to_use = temp_se->notification_interval;
		}

		/* else use the shortest of all valid escalation intervals */
		else if (temp_se->notification_interval < interval_to_use)
			interval_to_use = temp_se->notification_interval;

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "New interval: %f\n", interval_to_use);
	}

	/* if notification interval is 0, we shouldn't send any more problem notifications (unless service is volatile) */
	if (interval_to_use == 0.0 && svc->is_volatile == FALSE)
		svc->no_more_notifications = TRUE;
	else
		svc->no_more_notifications = FALSE;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Interval used for calculating next valid notification time: %f\n", interval_to_use);

	/* calculate next notification time */
	next_notification = offset + (interval_to_use * interval_length);

	return next_notification;
}


/* calculates next acceptable re-notification time for a host */
time_t get_next_host_notification_time(host *hst, time_t offset)
{
	time_t next_notification = 0L;
	double interval_to_use = 0.0;
	objectlist *list;
	int have_escalated_interval = FALSE;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Calculating next valid notification time...\n");

	/* default notification interval */
	interval_to_use = hst->notification_interval;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Default interval: %f\n", interval_to_use);

	/* check all the host escalation entries for valid matches for this host (at its current notification number) */
	for (list = hst->escalation_list; list; list = list->next) {
		hostescalation *temp_he = (hostescalation *)list->object_ptr;

		/* interval < 0 means to use non-escalated interval */
		if (temp_he->notification_interval < 0.0)
			continue;

		/* skip this entry if it isn't appropriate */
		if (is_valid_escalation_for_host_notification(hst, temp_he, NOTIFICATION_OPTION_NONE) == FALSE)
			continue;

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Found a valid escalation w/ interval of %f\n", temp_he->notification_interval);

		/* if we haven't used a notification interval from an escalation yet, use this one */
		if (have_escalated_interval == FALSE) {
			have_escalated_interval = TRUE;
			interval_to_use = temp_he->notification_interval;
		}

		/* else use the shortest of all valid escalation intervals  */
		else if (temp_he->notification_interval < interval_to_use)
			interval_to_use = temp_he->notification_interval;

		log_debug_info(DEBUGL_NOTIFICATIONS, 2, "New interval: %f\n", interval_to_use);
	}

	/* if interval is 0, no more notifications should be sent */
	if (interval_to_use == 0.0)
		hst->no_more_notifications = TRUE;
	else
		hst->no_more_notifications = FALSE;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Interval used for calculating next valid notification time: %f\n", interval_to_use);

	/* calculate next notification time */
	next_notification = offset + (interval_to_use * interval_length);

	return next_notification;
}


/******************************************************************/
/***************** NOTIFICATION OBJECT FUNCTIONS ******************/
/******************************************************************/

/* given a contact name, find the notification entry for them for the list in memory */
static notification *find_notification(notification *notification_list, contact *cntct)
{
	notification *temp_notification = NULL;

	if (cntct == NULL)
		return NULL;

	for (temp_notification = notification_list; temp_notification != NULL; temp_notification = temp_notification->next) {
		if (temp_notification->contact == cntct)
			return temp_notification;
	}

	/* we couldn't find the contact in the notification list */
	return NULL;
}


/* add a new notification to the list in memory */
int add_notification(notification **notification_list, nagios_macros *mac, contact *cntct)
{
	notification *new_notification = NULL;
	notification *temp_notification = NULL;

	if (cntct == NULL)
		return ERROR;

	log_debug_info(DEBUGL_NOTIFICATIONS, 2, "Adding contact '%s' to notification list.\n", cntct->name);

	/* don't add anything if this contact is already on the notification list */
	if ((temp_notification = find_notification(*notification_list, cntct)) != NULL)
		return OK;

	/* allocate memory for a new contact in the notification list */
	new_notification = nm_malloc(sizeof(notification));

	/* fill in the contact info */
	new_notification->contact = cntct;

	/* add new notification to head of list */
	new_notification->next = *notification_list;
	*notification_list = new_notification;

	/* add contact to notification recipients macro */
	if (mac->x[MACRO_NOTIFICATIONRECIPIENTS] == NULL)
		mac->x[MACRO_NOTIFICATIONRECIPIENTS] = nm_strdup(cntct->name);
	else {
		mac->x[MACRO_NOTIFICATIONRECIPIENTS] = nm_realloc(mac->x[MACRO_NOTIFICATIONRECIPIENTS], strlen(mac->x[MACRO_NOTIFICATIONRECIPIENTS]) + strlen(cntct->name) + 2);
		strcat(mac->x[MACRO_NOTIFICATIONRECIPIENTS], ",");
		strcat(mac->x[MACRO_NOTIFICATIONRECIPIENTS], cntct->name);
	}

	return OK;
}
