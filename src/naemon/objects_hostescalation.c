#include "objects_contactgroup.h"
#include "objects_hostescalation.h"
#include "objects_timeperiod.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"
#include "objects_fcache.h"

hostescalation *add_hostescalation(char *host_name, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options)
{
	hostescalation *new_hostescalation = NULL;
	host *h;
	timeperiod *tp = NULL;

	/* make sure we have the data we need */
	if (host_name == NULL || !*host_name) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host escalation host name is NULL\n");
		return NULL;
	}
	if (!(h = find_host(host_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host '%s' has an escalation, but is not defined anywhere!\n", host_name);
		return NULL;
	}
	if (escalation_period && !(tp = find_timeperiod(escalation_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Unable to locate timeperiod '%s' for hostescalation '%s'\n",
		       escalation_period, host_name);
		return NULL;
	}

	new_hostescalation = nm_calloc(1, sizeof(*new_hostescalation));

	/* add the escalation to its host */
	if (prepend_object_to_objectlist(&h->escalation_list, new_hostescalation) != OK) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add hostescalation to host '%s'\n", host_name);
		free(new_hostescalation);
		return NULL;
	}

	/* assign vars. Object names are immutable, so no need to copy */
	new_hostescalation->host_name = h->name;
	new_hostescalation->host_ptr = h;
	new_hostescalation->escalation_period = tp ? tp->name : NULL;
	new_hostescalation->escalation_period_ptr = tp;
	new_hostescalation->first_notification = first_notification;
	new_hostescalation->last_notification = last_notification;
	new_hostescalation->notification_interval = (notification_interval <= 0) ? 0 : notification_interval;
	new_hostescalation->escalation_options = escalation_options;

	new_hostescalation->id = num_objects.hostescalations++;
	return new_hostescalation;
}

void destroy_hostescalation(hostescalation *this_hostescalation)
{
	contactsmember *this_contactsmember;
	/* free memory for the contact group members */
	contactgroupsmember *this_contactgroupsmember;
	if (!this_hostescalation)
		return;
	this_contactgroupsmember = this_hostescalation->contact_groups;

	while (this_contactgroupsmember != NULL) {
		contactgroupsmember *next_contactgroupsmember = this_contactgroupsmember->next;
		nm_free(this_contactgroupsmember);
		this_contactgroupsmember = next_contactgroupsmember;
	}

	/* free memory for contacts */
	this_contactsmember = this_hostescalation->contacts;
	while (this_contactsmember != NULL) {
		contactsmember *next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}
	nm_free(this_hostescalation);
	num_objects.hostescalations--;
}

contactgroupsmember *add_contactgroup_to_hostescalation(hostescalation *he, char *group_name)
{
	return add_contactgroup_to_object(&he->contact_groups, group_name);
}

contactsmember *add_contact_to_hostescalation(hostescalation *he, char *contact_name)
{
	return add_contact_to_object(&he->contacts, contact_name);
}

void nm_fcache_hostescalation(struct nm_writebuf *wb, const hostescalation *temp_hostescalation)
{
	nm_wb_lit(wb, "define hostescalation {\n");
	fc_str(wb, "host_name", temp_hostescalation->host_name);
	fc_int(wb, "first_notification", temp_hostescalation->first_notification);
	fc_int(wb, "last_notification", temp_hostescalation->last_notification);
	fc_dbl(wb, "notification_interval", temp_hostescalation->notification_interval);
	if (temp_hostescalation->escalation_period)
		fc_str(wb, "escalation_period", temp_hostescalation->escalation_period);
	fc_str(wb, "escalation_options", opts2str(temp_hostescalation->escalation_options, host_flag_map, 'r'));

	nm_fcache_contactlist(wb, "\tcontacts\t", temp_hostescalation->contacts);
	nm_fcache_contactgrouplist(wb, "\tcontact_groups\t", temp_hostescalation->contact_groups);
	nm_wb_lit(wb, "\t}\n\n");
}

void fcache_hostescalation(FILE *fp, const hostescalation *temp_hostescalation)
{
	struct nm_writebuf wb;

	fc_file_begin(&wb, fp);
	nm_fcache_hostescalation(&wb, temp_hostescalation);
	nm_writebuf_done(&wb);
}
