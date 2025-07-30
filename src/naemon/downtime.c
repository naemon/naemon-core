#include "config.h"
#include "common.h"
#include "comments.h"
#include "downtime.h"
#include "statusdata.h"
#include "broker.h"
#include "events.h"
#include "notifications.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"
#include <string.h>
#include <glib.h>


scheduled_downtime *scheduled_downtime_list = NULL;
int		   defer_downtime_sorting = 0;
static GHashTable *dt_hashtable;


#define DT_ENULL (-1)
#define DT_EHOST (-2)
#define DT_ESERVICE (-3)
#define DT_ETYPE (-4)
#define DT_ETRIGGER (-5)
#define DT_ETIME (-6)


static void check_for_expired_downtime(struct nm_event_execution_properties *evprop);
static int handle_scheduled_downtime_start(scheduled_downtime *dt);
static int handle_scheduled_downtime_stop(scheduled_downtime *dt);

static const char *dt_strerror(int err)
{
	if (err > 0)
		return strerror(err);

	switch (err) {
	case DT_ENULL: return "NULL pointer";
	case DT_EHOST: return "No hostname, or host not found";
	case DT_ESERVICE: return "No service_description, or service not found";
	case DT_ETYPE: return "Invalid downtime type, or type/data mismatch";
	case DT_ETRIGGER: return "Triggering downtime not found";
	case DT_ETIME: return "Bad time spec";
	}
	return "Unknown error";
}


static int downtime_compar(const void *p1, const void *p2)
{
	scheduled_downtime *d1 = *(scheduled_downtime **)p1;
	scheduled_downtime *d2 = *(scheduled_downtime **)p2;

	/*
		If the start times of two downtimes are equal and one is triggered
		but the other is not, the triggered downtime should be later in the
		list than the untriggered one. This is so they are written to the
		retention.dat and status.dat in the correct order.

		Previously the triggered downtime always appeared before its
		triggering downtime in those files. When the downtimes were read
		from those files, either on a core restart or by the CGIs, the
		triggered downtime would be discarded because the triggering
		downtime did not yet exist.

		The most common case for this is when a downtime is created and
		the option is selected to create triggered downtimes on all child
		objects. This change in the sort order does NOT resolve the
		case where a manually created, triggered downtime is created with
		a start time earlier than the triggering downtime.

		This would need to be resolved by comparing the triggered_by value
		with the downtime ID regardless of the start time. However, this
		should be a relatively rare case and only caused by intentional
		scheduling by a human. This change was not implemented because it
		would cause the downtime list to be out of time order and the
		implications of this were not well understood.
	*/

	if (d1->start_time == d2->start_time) {
		if ((d1->triggered_by == 0 && d2->triggered_by != 0) ||
		    (d1->triggered_by != 0 && d2->triggered_by == 0)) {
			return d1->triggered_by == 0 ? -1 : 1;
		}
	}
	return (d1->start_time < d2->start_time) ? -1 : (d1->start_time - d2->start_time);
}


static int downtime_add(scheduled_downtime *dt)
{
	scheduled_downtime *trigger = NULL;
	struct host *h;
	struct service *s;

	if (!dt)
		return DT_ENULL;

	log_debug_info(DEBUGL_DOWNTIME, 0, "downtime_add(): id=%lu; type=%s; host=%s; service=%s\n",
	               dt->downtime_id,
	               dt->type == HOST_DOWNTIME ? "host" : "service",
	               dt->host_name, dt->service_description);
	/*
	 * check for errors.
	 * host_name must always be set
	 */
	if (!dt->host_name)
		return DT_EHOST;

	/* service_description should only be set for service downtime */
	if ((dt->type == HOST_DOWNTIME) != (!dt->service_description))
		return DT_ETYPE;
	/* type must be either SERVICE_DOWNTIME or HOST_DOWNTIME */
	if (dt->type != SERVICE_DOWNTIME && dt->type != HOST_DOWNTIME)
		return DT_ETYPE;
	/* triggered downtime must be triggered by an existing downtime */
	if (dt->triggered_by && !(trigger = find_downtime(ANY_DOWNTIME, dt->triggered_by)))
		return DT_ETRIGGER;
	/* non-triggered downtime must have start_time < end_time */
	if (!trigger && dt->start_time >= dt->end_time)
		return DT_ETIME;
	/* flexible downtime must have a duration */
	if (!dt->fixed && !dt->duration)
		return DT_ETIME;

	/* the object we're adding downtime for must exist */
	if (!dt->service_description) {
		if (!(h = find_host(dt->host_name)))
			return DT_EHOST;
	} else if (!(s = find_service(dt->host_name, dt->service_description))) {
		return DT_ESERVICE;
	}

	/* set downtime_id if not already set */
	if (!dt->downtime_id) {
		dt->downtime_id = next_downtime_id++;
	} else if (dt->downtime_id > next_downtime_id) {
		next_downtime_id = dt->downtime_id + 1;
	}

	g_hash_table_insert(dt_hashtable, GINT_TO_POINTER(dt->downtime_id), dt);

	if (defer_downtime_sorting || !scheduled_downtime_list ||
	    downtime_compar(&dt, &scheduled_downtime_list) < 0) {
		if (scheduled_downtime_list) {
			scheduled_downtime_list->prev = dt;
		}
		dt->next = scheduled_downtime_list;
		dt->prev = NULL;
		scheduled_downtime_list = dt;
	} else {
		scheduled_downtime *cur;

		/* add new downtime to downtime list, sorted by start time */
		for (cur = scheduled_downtime_list; cur; cur = cur->next) {
			if (downtime_compar(&dt, &cur) < 0) {
				dt->prev = cur->prev;
				if (cur->prev)
					cur->prev->next = dt;
				dt->next = cur;
				cur->prev = dt;
				break;
			}
			if (!cur->next) {
				dt->next = NULL;
				cur->next = dt;
				dt->prev = cur;
				break;
			}
		}
	}
	return OK;
}


static void downtime_remove(scheduled_downtime *dt)
{
	g_hash_table_remove(dt_hashtable, GINT_TO_POINTER(dt->downtime_id));
	if (scheduled_downtime_list == dt) {
		scheduled_downtime_list = dt->next;
		if (scheduled_downtime_list)
			scheduled_downtime_list->prev = NULL;
	} else {
		dt->prev->next = dt->next;
		if (dt->next)
			dt->next->prev = dt->prev;
	}
}


/******************************************************************/
/**************** INITIALIZATION/CLEANUP FUNCTIONS ****************/
/******************************************************************/

/* initializes scheduled downtime data */
int initialize_downtime_data(void)
{
	dt_hashtable = g_hash_table_new(g_direct_hash, g_direct_equal);
	next_downtime_id = 1;
	return OK;
}


/* cleans up scheduled downtime data */
int cleanup_downtime_data(void)
{
	free_downtime_data();
	return OK;
}


/******************************************************************/
/********************** SCHEDULING FUNCTIONS **********************/
/******************************************************************/

static void handle_downtime_start_event(struct nm_event_execution_properties *evprop)
{
	if (evprop->user_data) {
		if (evprop->execution_type == EVENT_EXEC_NORMAL) {
			/* process scheduled downtime info */
			scheduled_downtime *temp_downtime = NULL;
			unsigned long downtime_id = *(unsigned long *)evprop->user_data;
			/* find the downtime entry */
			if ((temp_downtime = find_downtime(ANY_DOWNTIME, downtime_id)) == NULL) {
				log_debug_info(DEBUGL_DOWNTIME, 1, "Unable to find downtime id: %lu\n",
				               downtime_id);
				return;
			}

			/* NULL out this event's start time since the calling function,
			   handle_timed_event(), will free the event, this will prevent
			   unschedule_downtime from freeing something that has already been
			   freed. The start event is not needed within
			   handle_scheduled_downtime_start(). */
			temp_downtime->start_event = NULL;

			/* handle the downtime */
			handle_scheduled_downtime_start(temp_downtime);

		}
		nm_free(evprop->user_data);
	}
}
static void handle_downtime_stop_event(struct nm_event_execution_properties *evprop)
{
	if (evprop->user_data) {
		if (evprop->execution_type == EVENT_EXEC_NORMAL) {
			/* process scheduled downtime info */
			scheduled_downtime *temp_downtime = NULL;
			unsigned long downtime_id = *(unsigned long *)evprop->user_data;

			/* find the downtime entry */
			if ((temp_downtime = find_downtime(ANY_DOWNTIME, downtime_id)) == NULL) {
				log_debug_info(DEBUGL_DOWNTIME, 1, "Unable to find downtime id: %lu\n",
				               downtime_id);
				return;
			}

			/* NULL out this event's stop time since the calling function,
			   handle_timed_event(), will free the event, this will prevent
			   unschedule_downtime from freeing something that has already been
			   freed. The stop event is not needed within
			   handle_scheduled_downtime_stop(). */
			temp_downtime->stop_event = NULL;

			/* handle the downtime */
			handle_scheduled_downtime_stop(temp_downtime);
		}
		nm_free(evprop->user_data);
	}
}

/* schedules a host or service downtime */
int schedule_downtime(int type, char *host_name, char *service_description, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long *new_downtime_id)
{

	unsigned long downtime_id = 0L;
	g_return_val_if_fail(dt_hashtable != NULL, ERROR);

	/* don't add old or invalid downtimes */
	if (start_time >= end_time || end_time <= time(NULL)) {
		log_debug_info(DEBUGL_DOWNTIME, 1, "Invalid start (%lu) or end (%lu) times\n",
		               start_time, end_time);
		return ERROR;
	}

	/* add a new downtime entry */
	add_new_downtime(type, host_name, service_description, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration, &downtime_id, FALSE, FALSE);

	/* register the scheduled downtime */
	register_downtime(type, downtime_id);

	/* return downtime id */
	if (new_downtime_id != NULL)
		*new_downtime_id = downtime_id;

	return OK;
}


/* unschedules a host or service downtime */
int unschedule_downtime(int type, unsigned long downtime_id)
{
	scheduled_downtime *temp_downtime = NULL;
	scheduled_downtime *next_downtime = NULL;
	host *hst = NULL;
	service *svc = NULL;
	int attr = 0;

	/* find the downtime entry in the list in memory */
	if ((temp_downtime = find_downtime(type, downtime_id)) == NULL)
		return ERROR;

	/* find the host or service associated with this downtime */
	if (temp_downtime->type == HOST_DOWNTIME) {
		if ((hst = find_host(temp_downtime->host_name)) == NULL)
			return ERROR;
	} else {
		if ((svc = find_service(temp_downtime->host_name, temp_downtime->service_description)) == NULL)
			return ERROR;
	}

	log_debug_info(DEBUGL_DOWNTIME, 0, "Cancelling %s downtime (id=%lu)\n",
	               temp_downtime->type == HOST_DOWNTIME ? "host" : "service",
	               temp_downtime->downtime_id);

	/* decrement the downtime depth variable and update status data if necessary */
	if (temp_downtime->is_in_effect == TRUE) {

		attr = NEBATTR_DOWNTIME_STOP_CANCELLED;
		broker_downtime_data(NEBTYPE_DOWNTIME_STOP, NEBFLAG_NONE, attr, temp_downtime->type, temp_downtime->host_name, temp_downtime->service_description, temp_downtime->entry_time, temp_downtime->author, temp_downtime->comment, temp_downtime->start_time, temp_downtime->end_time, temp_downtime->fixed, temp_downtime->triggered_by, temp_downtime->duration, temp_downtime->downtime_id);

		if (temp_downtime->type == HOST_DOWNTIME) {

			if (hst->scheduled_downtime_depth > 0)
				hst->scheduled_downtime_depth--;
			update_host_status(hst, FALSE);

			/* log a notice - this is parsed by the history CGI */
			if (hst->scheduled_downtime_depth == 0) {

				nm_log(NSLOG_INFO_MESSAGE, "HOST DOWNTIME ALERT: %s;CANCELLED; Scheduled downtime for host has been cancelled.\n", hst->name);

				/* send a notification */
				host_notification(hst, NOTIFICATION_DOWNTIMECANCELLED, NULL, NULL, NOTIFICATION_OPTION_NONE);
			}
		}

		else {

			if (svc->scheduled_downtime_depth > 0)
				svc->scheduled_downtime_depth--;
			update_service_status(svc, FALSE);

			/* log a notice - this is parsed by the history CGI */
			if (svc->scheduled_downtime_depth == 0) {

				nm_log(NSLOG_INFO_MESSAGE, "SERVICE DOWNTIME ALERT: %s;%s;CANCELLED; Scheduled downtime for service has been cancelled.\n", svc->host_name, svc->description);

				/* send a notification */
				service_notification(svc, NOTIFICATION_DOWNTIMECANCELLED, NULL, NULL, NOTIFICATION_OPTION_NONE);
			}
		}
	}

	/* remove scheduled entries from event queue */
	if (temp_downtime->start_event) {
		destroy_event(temp_downtime->start_event);
		temp_downtime->start_event = NULL;
	}
	if (temp_downtime->stop_event) {
		destroy_event(temp_downtime->stop_event);
		temp_downtime->stop_event = NULL;
	}

	/* just to be consistent, put it out of effect*/
	temp_downtime->is_in_effect = FALSE;

	/* delete downtime entry */
	if (temp_downtime->type == HOST_DOWNTIME)
		delete_host_downtime(downtime_id);
	else
		delete_service_downtime(downtime_id);

	/*
	 * unschedule all downtime entries that were triggered by this one
	 * @TODO: Fix this algorithm so it uses something sane instead
	 * of this horrible mess of recursive O(n * t), where t is
	 * "downtime triggered by this downtime"
	 */
	while (1) {

		for (temp_downtime = scheduled_downtime_list; temp_downtime != NULL; temp_downtime = next_downtime) {
			next_downtime = temp_downtime->next;
			if (temp_downtime->triggered_by == downtime_id) {
				unschedule_downtime(ANY_DOWNTIME, temp_downtime->downtime_id);
				break;
			}
		}

		if (temp_downtime == NULL)
			break;
	}

	return OK;
}


/* registers scheduled downtime (schedules it, adds comments, etc.) */
int register_downtime(int type, unsigned long downtime_id)
{
	char *temp_buffer = NULL;
	char start_time_string[MAX_DATETIME_LENGTH] = "";
	char flex_start_string[MAX_DATETIME_LENGTH] = "";
	char end_time_string[MAX_DATETIME_LENGTH] = "";
	scheduled_downtime *temp_downtime = NULL;
	host *hst = NULL;
	service *svc = NULL;
	const char *type_string = NULL;
	int hours = 0;
	int minutes = 0;
	int seconds = 0;
	unsigned long *new_downtime_id = NULL;

	/* find the downtime entry in memory */
	temp_downtime = find_downtime(type, downtime_id);
	if (temp_downtime == NULL) {
		log_debug_info(DEBUGL_DOWNTIME, 0, "Cannot find downtime ID: %lu\n", downtime_id);
		return ERROR;
	}

	/* find the host or service associated with this downtime */
	if (temp_downtime->type == HOST_DOWNTIME) {
		if ((hst = find_host(temp_downtime->host_name)) == NULL) {
			log_debug_info(DEBUGL_DOWNTIME, 1,
			               "Cannot find host (%s) for downtime ID: %lu\n",
			               temp_downtime->host_name, downtime_id);
			return ERROR;
		}
	} else {
		if ((svc = find_service(temp_downtime->host_name, temp_downtime->service_description)) == NULL) {
			log_debug_info(DEBUGL_DOWNTIME, 1,
			               "Cannot find service (%s) for host (%s) for downtime ID: %lu\n",
			               temp_downtime->service_description, temp_downtime->host_name,
			               downtime_id);
			return ERROR;
		}
	}

	/* create the comment */
	get_datetime_string(&(temp_downtime->start_time), start_time_string, MAX_DATETIME_LENGTH, SHORT_DATE_TIME);
	get_datetime_string(&(temp_downtime->flex_downtime_start), flex_start_string, MAX_DATETIME_LENGTH, SHORT_DATE_TIME);
	get_datetime_string(&(temp_downtime->end_time), end_time_string, MAX_DATETIME_LENGTH, SHORT_DATE_TIME);
	hours = temp_downtime->duration / 3600;
	minutes = ((temp_downtime->duration - (hours * 3600)) / 60);
	seconds = temp_downtime->duration - (hours * 3600) - (minutes * 60);
	if (temp_downtime->type == HOST_DOWNTIME)
		type_string = "host";
	else
		type_string = "service";
	if (temp_downtime->fixed == TRUE)
		nm_asprintf(&temp_buffer, "This %s has been scheduled for fixed downtime from %s to %s.  Notifications for the %s will not be sent out during that time period.", type_string, start_time_string, end_time_string, type_string);
	else
		nm_asprintf(&temp_buffer, "This %s has been scheduled for flexible downtime starting between %s and %s and lasting for a period of %d hours and %d minutes.  Notifications for the %s will not be sent out during that time period.", type_string, start_time_string, end_time_string, hours, minutes, type_string);


	log_debug_info(DEBUGL_DOWNTIME, 0, "Scheduled Downtime Details:\n");
	if (temp_downtime->type == HOST_DOWNTIME) {
		log_debug_info(DEBUGL_DOWNTIME, 0, " Type:        Host Downtime\n");
		log_debug_info(DEBUGL_DOWNTIME, 0, " Host:        %s\n", hst->name);
	} else {
		log_debug_info(DEBUGL_DOWNTIME, 0, " Type:        Service Downtime\n");
		log_debug_info(DEBUGL_DOWNTIME, 0, " Host:        %s\n", svc->host_name);
		log_debug_info(DEBUGL_DOWNTIME, 0, " Service:     %s\n", svc->description);
	}
	log_debug_info(DEBUGL_DOWNTIME, 0, " Fixed/Flex:  %s\n", (temp_downtime->fixed == TRUE) ? "Fixed" : "Flexible");
	log_debug_info(DEBUGL_DOWNTIME, 0, " Start:       %s\n", start_time_string);
	if (temp_downtime->flex_downtime_start) {
		log_debug_info(DEBUGL_DOWNTIME, 0, " Flex Start:  %s\n", flex_start_string);
	}
	log_debug_info(DEBUGL_DOWNTIME, 0, " End:         %s\n", end_time_string);
	log_debug_info(DEBUGL_DOWNTIME, 0, " Duration:    %dh %dm %ds\n", hours, minutes, seconds);
	log_debug_info(DEBUGL_DOWNTIME, 0, " Downtime ID: %lu\n", temp_downtime->downtime_id);
	log_debug_info(DEBUGL_DOWNTIME, 0, " Trigger ID:  %lu\n", temp_downtime->triggered_by);


	/* add a non-persistent comment to the host or service regarding the scheduled outage */
	if (find_comment(temp_downtime->comment_id, HOST_COMMENT | SERVICE_COMMENT) == NULL) {
		if (temp_downtime->type == SERVICE_DOWNTIME)
			add_new_comment(SERVICE_COMMENT, DOWNTIME_COMMENT, svc->host_name, svc->description, time(NULL), (NULL == temp_downtime->author ? "(Naemon Process)" : temp_downtime->author), temp_buffer, 0, COMMENTSOURCE_INTERNAL, FALSE, (time_t)0, &(temp_downtime->comment_id));
		else
			add_new_comment(HOST_COMMENT, DOWNTIME_COMMENT, hst->name, NULL, time(NULL), (NULL == temp_downtime->author ? "(Naemon Process)" : temp_downtime->author), temp_buffer, 0, COMMENTSOURCE_INTERNAL, FALSE, (time_t)0, &(temp_downtime->comment_id));
	}

	nm_free(temp_buffer);
	if (temp_downtime->is_in_effect) { /* in effect, so schedule a stop event*/
		time_t event_time = 0L;
		/* schedule an event to end the downtime */
		if (temp_downtime->fixed == FALSE) {
			event_time = (time_t)((unsigned long)temp_downtime->flex_downtime_start
			                      + temp_downtime->duration);
		} else {
			event_time = temp_downtime->end_time;
		}

		new_downtime_id = nm_malloc(sizeof(unsigned long));
		*new_downtime_id = temp_downtime->downtime_id;

		schedule_event(event_time - time(NULL), handle_downtime_stop_event, (void *)new_downtime_id);
	} else { /* not in effect, schedule a start event if necessary (or expiry event for flexible downtime)*/
		if (!temp_downtime->fixed) {
			/**
			 * Since a flex downtime may never start, schedule an expiring event in
			 * case the event is never triggered. The expire event will NOT cancel
			 * a downtime event that is in effect, since that will be cancelled
			 *	when its "handle_downtime_stop_event" event is invoked (scheduled above)
			 */
			log_debug_info(DEBUGL_DOWNTIME, 1, "Scheduling downtime expire event in case flexible downtime is never triggered\n");
			temp_downtime->stop_event = schedule_event((temp_downtime->end_time + 1) - time(NULL), check_for_expired_downtime, NULL);
			if (temp_downtime->flex_downtime_start > 0) {
				new_downtime_id = nm_malloc(sizeof(unsigned long));
				*new_downtime_id = downtime_id;
				temp_downtime->start_event = schedule_event(temp_downtime->flex_downtime_start - time(NULL), handle_downtime_start_event, (void *)new_downtime_id);

			}
		}

		/* No need to schedule triggered downtimes, since they're ... triggered*/
		else if (temp_downtime->triggered_by == 0) {
			new_downtime_id = nm_malloc(sizeof(unsigned long));
			*new_downtime_id = downtime_id;
			temp_downtime->start_event = schedule_event(temp_downtime->start_time - time(NULL), handle_downtime_start_event, (void *)new_downtime_id);

		}

	}

	return OK;
}


/* handles scheduled downtime (id passed from timed event queue) */
int handle_scheduled_downtime_by_id(unsigned long downtime_id)
{
	scheduled_downtime *temp_downtime = NULL;

	/* find the downtime entry */
	if ((temp_downtime = find_downtime(ANY_DOWNTIME, downtime_id)) == NULL) {
		log_debug_info(DEBUGL_DOWNTIME, 1, "Unable to find downtime id: %lu\n",
		               downtime_id);
		return ERROR;
	}

	/* NULL out this event's start time since the calling function,
		handle_timed_event(), will free the event, this will prevent
		unschedule_downtime from freeing something that has already been
		freed. The start event is not needed within
		handle_scheduled_downtime(). */
	temp_downtime->start_event = NULL;

	/* handle the downtime */
	return handle_scheduled_downtime(temp_downtime);
}

static int handle_scheduled_downtime_stop(scheduled_downtime *temp_downtime)
{
	scheduled_downtime *this_downtime = NULL;
	host *hst = NULL;
	service *svc = NULL;
	int attr = 0;

	if (temp_downtime == NULL)
		return ERROR;

	/* find the host or service associated with this downtime */
	if (temp_downtime->type == HOST_DOWNTIME) {
		if ((hst = find_host(temp_downtime->host_name)) == NULL) {
			log_debug_info(DEBUGL_DOWNTIME, 1, "Unable to find host (%s) for downtime\n", temp_downtime->host_name);
			return ERROR;
		}
	} else {
		if ((svc = find_service(temp_downtime->host_name, temp_downtime->service_description)) == NULL) {
			log_debug_info(DEBUGL_DOWNTIME, 1, "Unable to find service (%s) host (%s) for downtime\n", temp_downtime->service_description, temp_downtime->host_name);
			return ERROR;
		}
	}

	attr = NEBATTR_DOWNTIME_STOP_NORMAL;
	broker_downtime_data(NEBTYPE_DOWNTIME_STOP, NEBFLAG_NONE, attr, temp_downtime->type, temp_downtime->host_name, temp_downtime->service_description, temp_downtime->entry_time, temp_downtime->author, temp_downtime->comment, temp_downtime->start_time, temp_downtime->end_time, temp_downtime->fixed, temp_downtime->triggered_by, temp_downtime->duration, temp_downtime->downtime_id);

	/* decrement the downtime depth variable */
	if (temp_downtime->type == HOST_DOWNTIME) {
		if (hst->scheduled_downtime_depth > 0)
			hst->scheduled_downtime_depth--;
		else
			log_debug_info(DEBUGL_DOWNTIME, 0, "Host '%s' tried to exit from a period of scheduled downtime (id=%lu), but was already out of downtime.\n", hst->name, temp_downtime->downtime_id);
	} else {
		if (svc->scheduled_downtime_depth > 0)
			svc->scheduled_downtime_depth--;
		else
			log_debug_info(DEBUGL_DOWNTIME, 0, "Service '%s' on host '%s' tried to exited from a period of scheduled downtime (id=%lu), but was already out of downtime.\n", svc->description, svc->host_name, temp_downtime->downtime_id);
	}

	if (temp_downtime->type == HOST_DOWNTIME && hst->scheduled_downtime_depth == 0) {

		log_debug_info(DEBUGL_DOWNTIME, 0, "Host '%s' has exited from a period of scheduled downtime (id=%lu).\n", hst->name, temp_downtime->downtime_id);

		/* log a notice - this one is parsed by the history CGI */
		nm_log(NSLOG_INFO_MESSAGE, "HOST DOWNTIME ALERT: %s;STOPPED; Host has exited from a period of scheduled downtime", hst->name);

		/* send a notification */
		host_notification(hst, NOTIFICATION_DOWNTIMEEND, temp_downtime->author, temp_downtime->comment, NOTIFICATION_OPTION_NONE);
	}

	else if (temp_downtime->type == SERVICE_DOWNTIME && svc->scheduled_downtime_depth == 0) {

		log_debug_info(DEBUGL_DOWNTIME, 0, "Service '%s' on host '%s' has exited from a period of scheduled downtime (id=%lu).\n", svc->description, svc->host_name, temp_downtime->downtime_id);

		/* log a notice - this one is parsed by the history CGI */
		nm_log(NSLOG_INFO_MESSAGE, "SERVICE DOWNTIME ALERT: %s;%s;STOPPED; Service has exited from a period of scheduled downtime", svc->host_name, svc->description);

		/* send a notification */
		service_notification(svc, NOTIFICATION_DOWNTIMEEND, temp_downtime->author, temp_downtime->comment, NOTIFICATION_OPTION_NONE);
	}


	/* update the status data */
	if (temp_downtime->type == HOST_DOWNTIME)
		update_host_status(hst, FALSE);
	else
		update_service_status(svc, FALSE);

	/* handle (stop) downtime that is triggered by this one */
	while (1) {

		/* list contents might change by recursive calls, so we use this inefficient method to prevent segfaults */
		for (this_downtime = scheduled_downtime_list; this_downtime != NULL; this_downtime = this_downtime->next) {
			if (this_downtime->triggered_by == temp_downtime->downtime_id) {
				handle_scheduled_downtime(this_downtime);
				break;
			}
		}

		if (this_downtime == NULL)
			break;
	}

	temp_downtime->is_in_effect = FALSE;

	/* delete downtime entry */
	if (temp_downtime->type == HOST_DOWNTIME)
		delete_host_downtime(temp_downtime->downtime_id);
	else
		delete_service_downtime(temp_downtime->downtime_id);

	return OK;
}

static int handle_scheduled_downtime_start(scheduled_downtime *temp_downtime)
{

	scheduled_downtime *this_downtime = NULL;
	host *hst = NULL;
	service *svc = NULL;
	time_t event_time = 0L;
	unsigned long *new_downtime_id = NULL;

	if (temp_downtime == NULL)
		return ERROR;

	/* find the host or service associated with this downtime */
	if (temp_downtime->type == HOST_DOWNTIME) {
		if ((hst = find_host(temp_downtime->host_name)) == NULL) {
			log_debug_info(DEBUGL_DOWNTIME, 1, "Unable to find host (%s) for downtime\n", temp_downtime->host_name);
			return ERROR;
		}
	} else {
		if ((svc = find_service(temp_downtime->host_name, temp_downtime->service_description)) == NULL) {
			log_debug_info(DEBUGL_DOWNTIME, 1, "Unable to find service (%s) host (%s) for downtime\n", temp_downtime->service_description, temp_downtime->host_name);
			return ERROR;
		}
	}


	broker_downtime_data(NEBTYPE_DOWNTIME_START, NEBFLAG_NONE, NEBATTR_NONE, temp_downtime->type, temp_downtime->host_name, temp_downtime->service_description, temp_downtime->entry_time, temp_downtime->author, temp_downtime->comment, temp_downtime->start_time, temp_downtime->end_time, temp_downtime->fixed, temp_downtime->triggered_by, temp_downtime->duration, temp_downtime->downtime_id);

	if (temp_downtime->type == HOST_DOWNTIME && hst->scheduled_downtime_depth == 0) {

		log_debug_info(DEBUGL_DOWNTIME, 0, "Host '%s' has entered a period of scheduled downtime (id=%lu).\n", hst->name, temp_downtime->downtime_id);

		/* log a notice - this one is parsed by the history CGI */
		nm_log(NSLOG_INFO_MESSAGE, "HOST DOWNTIME ALERT: %s;STARTED; Host has entered a period of scheduled downtime", hst->name);

		/* send a notification */
		if (FALSE == temp_downtime->start_notification_sent) {
			host_notification(hst, NOTIFICATION_DOWNTIMESTART, temp_downtime->author, temp_downtime->comment, NOTIFICATION_OPTION_NONE);
			temp_downtime->start_notification_sent = TRUE;
		}
	}

	else if (temp_downtime->type == SERVICE_DOWNTIME && svc->scheduled_downtime_depth == 0) {

		log_debug_info(DEBUGL_DOWNTIME, 0, "Service '%s' on host '%s' has entered a period of scheduled downtime (id=%lu).\n", svc->description, svc->host_name, temp_downtime->downtime_id);

		/* log a notice - this one is parsed by the history CGI */
		nm_log(NSLOG_INFO_MESSAGE, "SERVICE DOWNTIME ALERT: %s;%s;STARTED; Service has entered a period of scheduled downtime", svc->host_name, svc->description);

		/* send a notification */
		if (FALSE == temp_downtime->start_notification_sent) {
			service_notification(svc, NOTIFICATION_DOWNTIMESTART, temp_downtime->author, temp_downtime->comment, NOTIFICATION_OPTION_NONE);
			temp_downtime->start_notification_sent = TRUE;
		}
	}

	/* increment the downtime depth variable */
	if (temp_downtime->type == HOST_DOWNTIME)
		hst->scheduled_downtime_depth++;
	else
		svc->scheduled_downtime_depth++;

	/* set the in effect flag */
	temp_downtime->is_in_effect = TRUE;

	/* update the status data */
	if (temp_downtime->type == HOST_DOWNTIME)
		update_host_status(hst, FALSE);
	else
		update_service_status(svc, FALSE);

	/* schedule an event to end the downtime */
	if (temp_downtime->fixed == FALSE) {
		event_time = (time_t)((unsigned long)temp_downtime->flex_downtime_start
		                      + temp_downtime->duration);
	} else {
		event_time = temp_downtime->end_time;
	}

	new_downtime_id = nm_malloc(sizeof(unsigned long));
	*new_downtime_id = temp_downtime->downtime_id;

	temp_downtime->stop_event = schedule_event(event_time - time(NULL), handle_downtime_stop_event, (void *)new_downtime_id);

	/* handle (start) downtime that is triggered by this one */
	for (this_downtime = scheduled_downtime_list; this_downtime != NULL; this_downtime = this_downtime->next) {
		if (this_downtime->triggered_by == temp_downtime->downtime_id) {
			/* Initialize the flex_downtime_start as it has not been initialized as a flexible downtime */
			this_downtime->flex_downtime_start = temp_downtime->flex_downtime_start;
			handle_scheduled_downtime(this_downtime);
		}
	}

	return OK;
}

/* handles scheduled host or service downtime */
int handle_scheduled_downtime(scheduled_downtime *temp_downtime)
{
	/* have we come to the end of the scheduled downtime? */
	if (temp_downtime->is_in_effect == TRUE)
		return handle_scheduled_downtime_stop(temp_downtime);
	/* else we are just starting the scheduled downtime */
	else
		return handle_scheduled_downtime_start(temp_downtime);
}


/* checks for flexible (non-fixed) host downtime that should start now
 *  * return: < 0 : error
 *         number of the host downtime will be started soon*/
int check_pending_flex_host_downtime(host *hst)
{
	scheduled_downtime *temp_downtime = NULL;
	time_t current_time = 0L;
	unsigned long *new_downtime_id = NULL;
	int num_downtimes_start = 0;

	if (hst == NULL)
		return ERROR;

	time(&current_time);

	/* if host is currently up, nothing to do */
	if (hst->current_state == STATE_UP)
		return OK;

	/* check all downtime entries */
	for (temp_downtime = scheduled_downtime_list; temp_downtime != NULL; temp_downtime = temp_downtime->next) {

		if (temp_downtime->type != HOST_DOWNTIME)
			continue;

		if (temp_downtime->fixed == TRUE)
			continue;

		if (temp_downtime->is_in_effect == TRUE)
			continue;

		/* triggered downtime entries should be ignored here */
		if (temp_downtime->triggered_by != 0)
			continue;

		/* this entry matches our host! */
		if (find_host(temp_downtime->host_name) == hst) {

			/* if the time boundaries are okay, start this scheduled downtime */
			if (temp_downtime->start_time <= current_time && current_time <= temp_downtime->end_time) {

				log_debug_info(DEBUGL_DOWNTIME, 0, "Flexible downtime (id=%lu) for host '%s' starting now...\n", temp_downtime->downtime_id, hst->name);
				temp_downtime->flex_downtime_start = current_time;

				new_downtime_id = nm_malloc(sizeof(unsigned long));
				*new_downtime_id = temp_downtime->downtime_id;

				temp_downtime->start_event = schedule_event(temp_downtime->flex_downtime_start - time(NULL), handle_downtime_start_event, (void *)new_downtime_id);
				num_downtimes_start++;
			}
		}
	}

	return num_downtimes_start;
}


/* checks for flexible (non-fixed) service downtime that should start now
 * return: < 0 : error
 *         number of the service downtime will be started soon*/
int check_pending_flex_service_downtime(service *svc)
{
	scheduled_downtime *temp_downtime = NULL;
	time_t current_time = 0L;
	unsigned long *new_downtime_id = NULL;
	int num_downtimes_start = 0;

	if (svc == NULL)
		return ERROR;

	time(&current_time);

	/* if service is currently ok, nothing to do */
	if (svc->current_state == STATE_OK)
		return OK;

	/* check all downtime entries */
	for (temp_downtime = scheduled_downtime_list; temp_downtime != NULL; temp_downtime = temp_downtime->next) {

		if (temp_downtime->type != SERVICE_DOWNTIME)
			continue;

		if (temp_downtime->fixed == TRUE)
			continue;

		if (temp_downtime->is_in_effect == TRUE)
			continue;

		/* triggered downtime entries should be ignored here */
		if (temp_downtime->triggered_by != 0)
			continue;

		/* this entry matches our service! */
		if (find_service(temp_downtime->host_name, temp_downtime->service_description) == svc) {

			/* if the time boundaries are okay, start this scheduled downtime */
			if (temp_downtime->start_time <= current_time && current_time <= temp_downtime->end_time) {

				log_debug_info(DEBUGL_DOWNTIME, 0, "Flexible downtime (id=%lu) for service '%s' on host '%s' starting now...\n", temp_downtime->downtime_id, svc->description, svc->host_name);

				temp_downtime->flex_downtime_start = current_time;

				new_downtime_id = nm_malloc(sizeof(unsigned long));
				*new_downtime_id = temp_downtime->downtime_id;

				temp_downtime->start_event = schedule_event(temp_downtime->flex_downtime_start - time(NULL), handle_downtime_start_event, (void *)new_downtime_id);
				num_downtimes_start++;
			}
		}
	}

	return num_downtimes_start;
}


/* event handler: checks for (and removes) expired downtime entries */
static void check_for_expired_downtime(struct nm_event_execution_properties *evprop)
{
	scheduled_downtime *temp_downtime = NULL;
	scheduled_downtime *next_downtime = NULL;
	time_t current_time = 0L;
	service *svc = NULL;
	host *hst = NULL;

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
		time(&current_time);

		/* check all downtime entries... */
		for (temp_downtime = scheduled_downtime_list; temp_downtime != NULL; temp_downtime = next_downtime) {

			next_downtime = temp_downtime->next;

			/* this entry should be removed */
			if (temp_downtime->is_in_effect == FALSE && temp_downtime->end_time <= current_time) {

				log_debug_info(DEBUGL_DOWNTIME, 0, "Expiring %s downtime (id=%lu)...\n", (temp_downtime->type == HOST_DOWNTIME) ? "host" : "service", temp_downtime->downtime_id);

				/* find the host or service associated with this downtime */
				if (temp_downtime->type == HOST_DOWNTIME) {
					if ((hst = find_host(temp_downtime->host_name)) == NULL) {
						log_debug_info(DEBUGL_DOWNTIME, 1,
						               "Unable to find host (%s) for downtime\n",
						               temp_downtime->host_name);
						return; /* ERROR */
					}

					/* send a notification */
					host_notification(hst, NOTIFICATION_DOWNTIMEEND,
					                  temp_downtime->author, temp_downtime->comment,
					                  NOTIFICATION_OPTION_NONE);
				} else {
					if ((svc = find_service(temp_downtime->host_name,
					                        temp_downtime->service_description)) == NULL) {
						log_debug_info(DEBUGL_DOWNTIME, 1,
						               "Unable to find service (%s) host (%s) for downtime\n",
						               temp_downtime->service_description,
						               temp_downtime->host_name);
						return; /* ERROR */
					}

					/* send a notification */
					service_notification(svc, NOTIFICATION_DOWNTIMEEND,
					                     temp_downtime->author, temp_downtime->comment,
					                     NOTIFICATION_OPTION_NONE);
				}

				/* delete the downtime entry */
				if (temp_downtime->type == HOST_DOWNTIME)
					delete_host_downtime(temp_downtime->downtime_id);
				else
					delete_service_downtime(temp_downtime->downtime_id);
			}
		}
	}
}


/******************************************************************/
/************************* SAVE FUNCTIONS *************************/
/******************************************************************/

/* save a host or service downtime */
int add_new_downtime(int type, char *host_name, char *service_description, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long *downtime_id, int is_in_effect, int start_notification_sent)
{
	int result = OK;

	if (type == HOST_DOWNTIME)
		return add_new_host_downtime(host_name, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration, downtime_id, is_in_effect, start_notification_sent);
	else
		return add_new_service_downtime(host_name, service_description, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration, downtime_id, is_in_effect, start_notification_sent);

	return result;
}


static unsigned long get_next_downtime_id(void)
{
	unsigned long new_dt_id;
	for (;;) {
		new_dt_id = next_downtime_id++;
		if (!find_downtime(ANY_DOWNTIME, next_downtime_id)) {
			return new_dt_id;
		}
	}
	return 0;
}


/* saves a host downtime entry */
int add_new_host_downtime(char *host_name, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long *downtime_id, int is_in_effect, int start_notification_sent)
{
	int result = OK;
	unsigned long new_downtime_id = 0L;

	if (host_name == NULL)
		return ERROR;

	if (!find_host(host_name)) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Ignoring request to add downtime for non-existing host '%s'\n",
		       host_name);
		return ERROR;
	}

	new_downtime_id = get_next_downtime_id();
	result = add_host_downtime(host_name, entry_time, author, comment_data, start_time, 0, end_time, fixed, triggered_by, duration, new_downtime_id, is_in_effect, start_notification_sent, NULL);

	/* save downtime id */
	if (downtime_id != NULL)
		*downtime_id = new_downtime_id;

	if (result == OK) {
		broker_downtime_data(NEBTYPE_DOWNTIME_ADD, NEBFLAG_NONE, NEBATTR_NONE, HOST_DOWNTIME, host_name, NULL, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration, new_downtime_id);
	}
	return result;
}


/* saves a service downtime entry */
int add_new_service_downtime(char *host_name, char *service_description, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long *downtime_id, int is_in_effect, int start_notification_sent)
{
	int result = OK;
	unsigned long new_downtime_id = 0L;

	if (host_name == NULL || service_description == NULL) {
		log_debug_info(DEBUGL_DOWNTIME, 1,
		               "Host name (%s) or service description (%s) is null\n",
		               ((NULL == host_name) ? "null" : host_name),
		               ((NULL == service_description) ? "null" : service_description));
		return ERROR;
	}

	if (!find_service(host_name, service_description)) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Ignoring request to add downtime to non-existing service '%s' on host '%s'\n",
		       service_description, host_name);
		return ERROR;
	}

	new_downtime_id = get_next_downtime_id();
	result = add_service_downtime(host_name, service_description, entry_time, author, comment_data, start_time, 0, end_time, fixed, triggered_by, duration, new_downtime_id, is_in_effect, start_notification_sent, NULL);

	/* save downtime id */
	if (downtime_id != NULL)
		*downtime_id = new_downtime_id;

	if (result == OK) {
		broker_downtime_data(NEBTYPE_DOWNTIME_ADD, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_DOWNTIME, host_name, service_description, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration, new_downtime_id);
	}

	return result;
}


/******************************************************************/
/*********************** DELETION FUNCTIONS ***********************/
/******************************************************************/

/* deletes a scheduled host or service downtime entry from the list in memory */
int delete_downtime(int type, unsigned long downtime_id)
{
	scheduled_downtime *this_downtime = NULL;

	/* find the downtime we should remove */
	this_downtime = find_downtime(type, downtime_id);
	if (!this_downtime)
		return ERROR;

	downtime_remove(this_downtime);

	/* first remove the comment associated with this downtime */
	if (this_downtime->type == HOST_DOWNTIME)
		delete_host_comment(this_downtime->comment_id);
	else
		delete_service_comment(this_downtime->comment_id);

	broker_downtime_data(NEBTYPE_DOWNTIME_DELETE, NEBFLAG_NONE, NEBATTR_NONE, type, this_downtime->host_name, this_downtime->service_description, this_downtime->entry_time, this_downtime->author, this_downtime->comment, this_downtime->start_time, this_downtime->end_time, this_downtime->fixed, this_downtime->triggered_by, this_downtime->duration, downtime_id);

	nm_free(this_downtime->host_name);
	nm_free(this_downtime->service_description);
	nm_free(this_downtime->author);
	nm_free(this_downtime->comment);
	nm_free(this_downtime);
	return OK;
}


int delete_host_downtime(unsigned long downtime_id)
{
	return delete_downtime(HOST_DOWNTIME, downtime_id);
}


int delete_service_downtime(unsigned long downtime_id)
{
	return delete_downtime(SERVICE_DOWNTIME, downtime_id);
}


/*
 * Deletes all host and service downtimes on a host by hostname,
 * optionally filtered by service description, start time and comment.
 * All char* must be set or NULL - "" will silently fail to match
 * Returns number deleted
 */
int delete_downtime_by_hostname_service_description_start_time_comment(char *hostname, char *service_description, time_t start_time, char *cmnt)
{
	scheduled_downtime *temp_downtime;
	scheduled_downtime *next_downtime;
	void *downtime_cpy;
	int deleted = 0;
	objectlist *matches = NULL, *tmp_match = NULL;

	/* Do not allow deletion of everything - must have at least 1 filter on */
	if (hostname == NULL && service_description == NULL && start_time == 0 && cmnt == NULL)
		return deleted;

	for (temp_downtime = scheduled_downtime_list; temp_downtime != NULL; temp_downtime = next_downtime) {
		next_downtime = temp_downtime->next;
		if (start_time != 0 && temp_downtime->start_time != start_time) {
			continue;
		}
		if (cmnt != NULL && strcmp(temp_downtime->comment, cmnt) != 0)
			continue;
		if (temp_downtime->type == HOST_DOWNTIME) {
			/* If service is specified, then do not delete the host downtime */
			if (service_description != NULL)
				continue;
			if (hostname != NULL && strcmp(temp_downtime->host_name, hostname) != 0)
				continue;
		} else if (temp_downtime->type == SERVICE_DOWNTIME) {
			if (hostname != NULL && strcmp(temp_downtime->host_name, hostname) != 0)
				continue;
			if (service_description != NULL && strcmp(temp_downtime->service_description, service_description) != 0)
				continue;
		}

		downtime_cpy = nm_malloc(sizeof(scheduled_downtime));
		memcpy(downtime_cpy, temp_downtime, sizeof(scheduled_downtime));
		prepend_object_to_objectlist(&matches, downtime_cpy);
		deleted++;
	}

	for (tmp_match = matches; tmp_match != NULL; tmp_match = tmp_match->next) {
		temp_downtime = (scheduled_downtime *)tmp_match->object_ptr;
		unschedule_downtime(temp_downtime->type, temp_downtime->downtime_id);
		nm_free(temp_downtime);
	}

	free_objectlist(&matches);

	return deleted;
}


/******************************************************************/
/******************** ADDITION FUNCTIONS **************************/
/******************************************************************/

/* adds a host downtime entry to the list in memory */
int add_host_downtime(char *host_name, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t flex_downtime_start, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long downtime_id, int is_in_effect, int start_notification_sent, unsigned long *comment_id)
{
	return add_downtime(HOST_DOWNTIME, host_name, NULL, entry_time, author, comment_data, start_time, flex_downtime_start, end_time, fixed, triggered_by, duration, downtime_id, is_in_effect, start_notification_sent, comment_id);
}


/* adds a service downtime entry to the list in memory */
int add_service_downtime(char *host_name, char *svc_description, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t flex_downtime_start, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long downtime_id, int is_in_effect, int start_notification_sent, unsigned long *comment_id)
{
	return add_downtime(SERVICE_DOWNTIME, host_name, svc_description, entry_time, author, comment_data, start_time, flex_downtime_start, end_time, fixed, triggered_by, duration, downtime_id, is_in_effect, start_notification_sent, comment_id);
}


/* adds a host or service downtime entry to the list in memory */
int add_downtime(int downtime_type, char *host_name, char *svc_description, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t flex_downtime_start, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration, unsigned long downtime_id, int is_in_effect, int start_notification_sent, unsigned long *comment_id)
{
	scheduled_downtime *new_downtime = NULL;
	int result = OK;

	/* we don't have enough info */
	if (host_name == NULL || (downtime_type == SERVICE_DOWNTIME && svc_description == NULL)) {
		log_debug_info(DEBUGL_DOWNTIME, 1,
		               "Host name (%s) or service description (%s) is null\n",
		               ((NULL == host_name) ? "null" : host_name),
		               ((NULL == svc_description) ? "null" : svc_description));
		return ERROR;
	}

	/* allocate memory for the downtime */
	new_downtime = nm_calloc(1, sizeof(scheduled_downtime));

	/* duplicate vars */
	new_downtime->host_name = nm_strdup(host_name);

	if (downtime_type == SERVICE_DOWNTIME) {
		new_downtime->service_description = nm_strdup(svc_description);
	}
	if (!result && author) {
		new_downtime->author = nm_strdup(author);
	}
	if (!result && comment_data) {
		new_downtime->comment = nm_strdup(comment_data);
	}

	new_downtime->type = downtime_type;
	new_downtime->entry_time = entry_time;
	new_downtime->start_time = start_time;
	new_downtime->flex_downtime_start = flex_downtime_start;
	new_downtime->end_time = end_time;
	new_downtime->fixed = (fixed > 0) ? TRUE : FALSE;
	new_downtime->triggered_by = triggered_by;
	new_downtime->duration = duration;
	new_downtime->downtime_id = downtime_id;
	new_downtime->is_in_effect = is_in_effect;
	new_downtime->start_notification_sent = start_notification_sent;
	new_downtime->start_event = (timed_event *)0;
	new_downtime->stop_event = (timed_event *)0;
	if (comment_id != NULL)
		new_downtime->comment_id = *comment_id;
	if (result != ERROR) {
		result = downtime_add(new_downtime);
		if (result) {
			if (new_downtime->type == SERVICE_DOWNTIME) {
				nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to add downtime for service '%s' on host '%s': %s\n",
				       new_downtime->service_description, new_downtime->host_name, dt_strerror(result));
			} else {
				nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to add downtime for host '%s': %s\n", new_downtime->host_name, dt_strerror(result));
			}
			result = ERROR;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		nm_free(new_downtime->comment);
		nm_free(new_downtime->author);
		nm_free(new_downtime->service_description);
		nm_free(new_downtime->host_name);
		nm_free(new_downtime);
		return ERROR;
	}

	broker_downtime_data(NEBTYPE_DOWNTIME_LOAD, NEBFLAG_NONE, NEBATTR_NONE, downtime_type, host_name, svc_description, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration, downtime_id);

	return OK;
}


int sort_downtime(void)
{
	scheduled_downtime **array, *temp_downtime;
	unsigned long i = 0, unsorted_downtimes = 0;

	if (!defer_downtime_sorting)
		return OK;
	defer_downtime_sorting = 0;

	temp_downtime = scheduled_downtime_list;
	while (temp_downtime != NULL) {
		temp_downtime = temp_downtime->next;
		unsorted_downtimes++;
	}

	if (!unsorted_downtimes)
		return OK;

	array = nm_malloc(sizeof(*array) * unsorted_downtimes);
	while (scheduled_downtime_list) {
		array[i++] = scheduled_downtime_list;
		scheduled_downtime_list = scheduled_downtime_list->next;
	}

	qsort((void *)array, i, sizeof(*array), downtime_compar);
	scheduled_downtime_list = temp_downtime = array[0];
	temp_downtime->prev = NULL;
	for (i = 1; i < unsorted_downtimes; i++) {
		temp_downtime->next = array[i];
		temp_downtime = temp_downtime->next;
		temp_downtime->prev = array[i - 1];
	}
	temp_downtime->next = NULL;
	nm_free(array);
	return OK;
}


/******************************************************************/
/************************ SEARCH FUNCTIONS ************************/
/******************************************************************/

/* finds a specific downtime entry */
scheduled_downtime *find_downtime(int type, unsigned long downtime_id)
{
	scheduled_downtime *dt = NULL;

	dt = g_hash_table_lookup(dt_hashtable, GINT_TO_POINTER(downtime_id));
	if (dt && (type == ANY_DOWNTIME || type == dt->type))
		return dt;
	return NULL;
}


/* finds a specific host downtime entry */
scheduled_downtime *find_host_downtime(unsigned long downtime_id)
{
	return find_downtime(HOST_DOWNTIME, downtime_id);
}


/* finds a specific service downtime entry */
scheduled_downtime *find_service_downtime(unsigned long downtime_id)
{
	return find_downtime(SERVICE_DOWNTIME, downtime_id);
}

/* get the total number of downtimes */
int number_of_downtimes()
{
	 return (int)g_hash_table_size(dt_hashtable);
}


/******************************************************************/
/********************* CLEANUP FUNCTIONS **************************/
/******************************************************************/

/* frees memory allocated for the scheduled downtime data */
void free_downtime_data(void)
{
	scheduled_downtime *this_downtime = NULL;
	scheduled_downtime *next_downtime = NULL;

	if(dt_hashtable != NULL)
		g_hash_table_destroy(dt_hashtable);
	dt_hashtable = NULL;

	/* free memory for the scheduled_downtime list */
	for (this_downtime = scheduled_downtime_list; this_downtime != NULL; this_downtime = next_downtime) {
		next_downtime = this_downtime->next;
		nm_free(this_downtime->host_name);
		nm_free(this_downtime->service_description);
		nm_free(this_downtime->author);
		nm_free(this_downtime->comment);
		nm_free(this_downtime);
	}

	/* reset list pointer */
	scheduled_downtime_list = NULL;

	return;
}
