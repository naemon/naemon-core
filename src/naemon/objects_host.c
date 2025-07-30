#include "objects_host.h"
#include "objects_contactgroup.h"
#include "objects_contact.h"
#include "objects_timeperiod.h"
#include "objects_hostdependency.h"
#include "objects_hostescalation.h"
#include "objectlist.h"
#include "logging.h"
#include "nm_alloc.h"
#include "utils.h"

#include <string.h>
#include <glib.h>

static GHashTable *host_hash_table = NULL;
host *host_list = NULL;
host **host_ary = NULL;

int init_objects_host(int elems)
{
	host_ary = nm_calloc(elems, sizeof(host *));
	host_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	return OK;
}

void destroy_objects_host()
{
	unsigned int i;
	for (i = 0; i < num_objects.hosts; i++) {
		host *this_host = host_ary[i];
		destroy_host(this_host);
	}
	host_list = NULL;
	if (host_hash_table)
		g_hash_table_destroy(host_hash_table);

	host_hash_table = NULL;
	nm_free(host_ary);
	num_objects.hosts = 0;
}

int compare_host(const void *_host1, const void *_host2)
{
	host *host1 = (host *)_host1;
	host *host2 = (host *)_host2;
	if (!host1 && host2)
		return -1;
	if (host1 && !host2)
		return 1;
	if (!host1 && !host2)
		return 0;
	return strcmp(host1->name, host2->name);
}

host *create_host(const char *name)
{
	host *new_host = NULL;

	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host name is NULL\n");
		return NULL;
	}

	if (contains_illegal_object_chars(name) == TRUE) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: The name of host '%s' contains one or more illegal characters.", name);
		return NULL;
	}

	new_host = nm_calloc(1, sizeof(*new_host));

	new_host->name = new_host->display_name = new_host->alias = new_host->address = nm_strdup(name);
	new_host->child_hosts = g_tree_new_full((GCompareDataFunc)my_strsorter, NULL, g_free, NULL);
	new_host->parent_hosts = g_tree_new_full((GCompareDataFunc)my_strsorter, NULL, g_free, NULL);
	new_host->check_type = CHECK_TYPE_ACTIVE;
	new_host->state_type = HARD_STATE;
	new_host->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
	new_host->acknowledgement_end_time = (time_t)0;
	new_host->check_options = CHECK_OPTION_NONE;


	return new_host;
}

int setup_host_variables(host *new_host, const char *display_name, const char *alias, const char *address, const char *check_period, int initial_state, double check_interval, double retry_interval, int max_attempts, int notification_options, double notification_interval, double first_notification_delay, const char *notification_period, int notifications_enabled, const char *check_command, int checks_enabled, int accept_passive_checks, const char *event_handler, int event_handler_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, const char *notes, const char *notes_url, const char *action_url, const char *icon_image, const char *icon_image_alt, const char *vrml_image, const char *statusmap_image, int x_2d, int y_2d, int have_2d_coords, double x_3d, double y_3d, double z_3d, int have_3d_coords, int retain_status_information, int retain_nonstatus_information, int obsess, unsigned int hourly_value)
{
	timeperiod *check_tp = NULL, *notify_tp = NULL;

	if (check_period && !(check_tp = find_timeperiod(check_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate check_period '%s' for host '%s'!\n",
		       check_period, new_host->name);
		return -1;
	}
	if (notification_period && !(notify_tp = find_timeperiod(notification_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate notification_period '%s' for host '%s'!\n",
		       notification_period, new_host->name);
		return -1;
	}
	/* check values */
	if (max_attempts <= 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: max_check_attempts must be a positive integer host '%s'\n", new_host->name);
		return -1;
	}
	if (check_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid check_interval value for host '%s'\n", new_host->name);
		return -1;
	}
	if (notification_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid notification_interval value for host '%s'\n", new_host->name);
		return -1;
	}
	if (first_notification_delay < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid first_notification_delay value for host '%s'\n", new_host->name);
		return -1;
	}
	if (freshness_threshold < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Invalid freshness_threshold value for host '%s'\n", new_host->name);
		return -1;
	}

	/* assign string vars */
	if (display_name)
		new_host->display_name = nm_strdup(display_name);
	if (alias)
		new_host->alias = nm_strdup(alias);
	if (address)
		new_host->address = nm_strdup(address);
	if (check_tp) {
		new_host->check_period = nm_strdup(check_tp->name);
		new_host->check_period_ptr = check_tp;
	}
	new_host->notification_period = notify_tp ? nm_strdup(notify_tp->name) : NULL;
	new_host->notification_period_ptr = notify_tp;
	if (check_command) {
		new_host->check_command = nm_strdup(check_command);
		new_host->check_command_ptr = find_bang_command(check_command);
		if (new_host->check_command_ptr == NULL) {
			nm_log(NSLOG_VERIFICATION_ERROR, "Error: Host check command '%s' specified for host '%s' is not defined anywhere!", new_host->check_command, new_host->name);
			return -1;
		}
	}
	if (event_handler) {
		new_host->event_handler = nm_strdup(event_handler);
		new_host->event_handler_ptr = find_bang_command(event_handler);
		if (new_host->event_handler_ptr == NULL) {
			nm_log(NSLOG_VERIFICATION_ERROR, "Error: Event handler command '%s' specified for host '%s' not defined anywhere", new_host->event_handler, new_host->name);
			return -1;
		}
	}
	new_host->notes = notes ? nm_strdup(notes) : NULL;
	new_host->notes_url = notes_url ? nm_strdup(notes_url) : NULL;
	new_host->action_url = action_url ? nm_strdup(action_url) : NULL;
	new_host->icon_image = icon_image ? nm_strdup(icon_image) : NULL;
	new_host->icon_image_alt = icon_image_alt ? nm_strdup(icon_image_alt) : NULL;
	new_host->vrml_image = vrml_image ? nm_strdup(vrml_image) : NULL;
	new_host->statusmap_image = statusmap_image ? nm_strdup(statusmap_image) : NULL;

	/* duplicate non-string vars */
	new_host->hourly_value = hourly_value;
	new_host->max_attempts = max_attempts;
	new_host->check_interval = check_interval;
	new_host->retry_interval = retry_interval;
	new_host->notification_interval = notification_interval;
	new_host->first_notification_delay = first_notification_delay;
	new_host->notification_options = notification_options;
	new_host->flap_detection_enabled = (flap_detection_enabled > 0) ? TRUE : FALSE;
	new_host->low_flap_threshold = low_flap_threshold;
	new_host->high_flap_threshold = high_flap_threshold;
	new_host->flap_detection_options = flap_detection_options;
	new_host->stalking_options = stalking_options;
	new_host->process_performance_data = (process_perfdata > 0) ? TRUE : FALSE;
	new_host->check_freshness = (check_freshness > 0) ? TRUE : FALSE;
	new_host->freshness_threshold = freshness_threshold;
	new_host->checks_enabled = (checks_enabled > 0) ? TRUE : FALSE;
	new_host->accept_passive_checks = (accept_passive_checks > 0) ? TRUE : FALSE;
	new_host->event_handler_enabled = (event_handler_enabled > 0) ? TRUE : FALSE;
	new_host->x_2d = x_2d;
	new_host->y_2d = y_2d;
	new_host->have_2d_coords = (have_2d_coords > 0) ? TRUE : FALSE;
	new_host->x_3d = x_3d;
	new_host->y_3d = y_3d;
	new_host->z_3d = z_3d;
	new_host->have_3d_coords = (have_3d_coords > 0) ? TRUE : FALSE;
	new_host->obsess = (obsess > 0) ? TRUE : FALSE;
	new_host->retain_status_information = (retain_status_information > 0) ? TRUE : FALSE;
	new_host->retain_nonstatus_information = (retain_nonstatus_information > 0) ? TRUE : FALSE;
	new_host->current_state = initial_state;
	new_host->last_state = initial_state;
	new_host->last_hard_state = initial_state;
	new_host->current_attempt = (initial_state == STATE_UP) ? 1 : max_attempts;
	new_host->notifications_enabled = (notifications_enabled > 0) ? TRUE : FALSE;

	return 0;
}

int register_host(host *new_host)
{

	g_return_val_if_fail(host_hash_table != NULL, ERROR);

	if ((find_host(new_host->name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host '%s' has already been defined\n", new_host->name);
		return ERROR;
	}

	g_hash_table_insert(host_hash_table, new_host->name, new_host);

	new_host->id = num_objects.hosts++;
	host_ary[new_host->id] = new_host;
	if (new_host->id)
		host_ary[new_host->id - 1]->next = new_host;
	else
		host_list = new_host;

	return OK;
}

static gboolean my_g_tree_visit_pick_one(gpointer key, gpointer value, gpointer data)
{
	gpointer *outptr = (gpointer *)data;
	*outptr = value;
	return TRUE; /* Stop traversal */
}

void destroy_host(host *this_host)
{
	struct servicesmember *this_servicesmember, *next_servicesmember;
	struct contactgroupsmember *this_contactgroupsmember, *next_contactgroupsmember;
	struct contactsmember *this_contactsmember, *next_contactsmember;
	struct customvariablesmember *this_customvariablesmember, *next_customvariablesmember;
	struct objectlist *slavelist;

	if (!this_host)
		return;

	/* free memory for service links */
	this_servicesmember = this_host->services;
	while (this_servicesmember != NULL) {
		next_servicesmember = this_servicesmember->next;
		nm_free(this_servicesmember);
		this_servicesmember = next_servicesmember;
	}

	/* free memory for contact groups */
	this_contactgroupsmember = this_host->contact_groups;
	while (this_contactgroupsmember != NULL) {
		next_contactgroupsmember = this_contactgroupsmember->next;
		nm_free(this_contactgroupsmember);
		this_contactgroupsmember = next_contactgroupsmember;
	}

	/* free memory for contacts */
	this_contactsmember = this_host->contacts;
	while (this_contactsmember != NULL) {
		next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}

	/* free memory for custom variables */
	this_customvariablesmember = this_host->custom_variables;
	while (this_customvariablesmember != NULL) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
		this_customvariablesmember = next_customvariablesmember;
	}

	for (slavelist = this_host->notify_deps; slavelist; slavelist = slavelist->next)
		destroy_hostdependency(slavelist->object_ptr);
	for (slavelist = this_host->exec_deps; slavelist; slavelist = slavelist->next)
		destroy_hostdependency(slavelist->object_ptr);
	for (slavelist = this_host->escalation_list; slavelist; slavelist = slavelist->next)
		destroy_hostescalation(slavelist->object_ptr);
	while (this_host->hostgroups_ptr)
		remove_host_from_hostgroup(this_host->hostgroups_ptr->object_ptr, this_host);

	if (this_host->child_hosts) {
		struct host *curhost = NULL;
		do {
			curhost = NULL;
			g_tree_foreach(this_host->child_hosts, my_g_tree_visit_pick_one, &curhost);
			if (curhost) {
				remove_parent_from_host(curhost, this_host);
			}
		} while (curhost != NULL);
		g_tree_unref(this_host->child_hosts);
		this_host->child_hosts = NULL;
	}
	if (this_host->parent_hosts) {
		struct host *curhost = NULL;
		do {
			curhost = NULL;
			g_tree_foreach(this_host->parent_hosts, my_g_tree_visit_pick_one, &curhost);
			if (curhost) {
				remove_parent_from_host(this_host, curhost);
			}
		} while (curhost != NULL);
		g_tree_unref(this_host->parent_hosts);
		this_host->parent_hosts = NULL;
	}

	if (this_host->display_name != this_host->name)
		nm_free(this_host->display_name);
	if (this_host->alias != this_host->name)
		nm_free(this_host->alias);
	if (this_host->address != this_host->name)
		nm_free(this_host->address);
	nm_free(this_host->name);
	nm_free(this_host->plugin_output);
	nm_free(this_host->long_plugin_output);
	nm_free(this_host->perf_data);
	free_objectlist(&this_host->comments_list);
	free_objectlist(&this_host->hostgroups_ptr);
	free_objectlist(&this_host->notify_deps);
	free_objectlist(&this_host->exec_deps);
	free_objectlist(&this_host->escalation_list);
	nm_free(this_host->check_command);
	nm_free(this_host->event_handler);
	nm_free(this_host->check_period);
	nm_free(this_host->notification_period);
	nm_free(this_host->notes);
	nm_free(this_host->notes_url);
	nm_free(this_host->action_url);
	nm_free(this_host->icon_image);
	nm_free(this_host->icon_image_alt);
	nm_free(this_host->vrml_image);
	nm_free(this_host->statusmap_image);
	nm_free(this_host->current_notification_id);
	nm_free(this_host->last_problem_id);
	nm_free(this_host->current_problem_id);
	nm_free(this_host);
}

host *find_host(const char *name)
{
	return name ? g_hash_table_lookup(host_hash_table, name) : NULL;
}

const char *host_state_name(int state)
{
	switch (state) {
	case STATE_UP: return "UP";
	case STATE_DOWN: return "DOWN";
	case STATE_UNREACHABLE: return "UNREACHABLE";
	}

	return "(unknown)";
}

int add_parent_to_host(host *hst, host *parent)
{
	/* make sure we have the data we need */
	if (hst == NULL || parent == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host is NULL or parent host name is NULL\n");
		return ERROR;
	}

	/* a host cannot be a parent/child of itself */
	if (hst == parent) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Host '%s' cannot be a child/parent of itself\n", hst->name);
		return ERROR;
	}

	g_tree_insert(hst->parent_hosts, g_strdup(parent->name), parent);
	g_tree_insert(parent->child_hosts, g_strdup(hst->name), hst);

	return OK;
}

int remove_parent_from_host(host *hst, host *parent)
{
	if (hst->parent_hosts) {
		g_tree_remove(hst->parent_hosts, parent->name);
	}
	if (parent->child_hosts) {
		g_tree_remove(parent->child_hosts, hst->name);
	}
	return 0;
}

/* add a new contactgroup to a host */
contactgroupsmember *add_contactgroup_to_host(host *hst, char *group_name)
{
	return add_contactgroup_to_object(&hst->contact_groups, group_name);
}

/* adds a contact to a host */
contactsmember *add_contact_to_host(host *hst, char *contact_name)
{

	return add_contact_to_object(&hst->contacts, contact_name);
}

/* adds a custom variable to a host */
customvariablesmember *add_custom_variable_to_host(host *hst, char *varname, char *varvalue)
{

	return add_custom_variable_to_object(&hst->custom_variables, varname, varvalue);
}

int get_host_count(void)
{
	return num_objects.hosts;
}

time_t get_host_check_interval_s(const host *hst)
{
	return hst->check_interval * interval_length;
}

time_t get_host_retry_interval_s(const host *hst)
{
	return hst->retry_interval * interval_length;
}


int is_contact_for_host(host *hst, contact *cntct)
{
	contactsmember *temp_contactsmember = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;

	if (hst == NULL || cntct == NULL) {
		return FALSE;
	}

	/* search all individual contacts of this host */
	for (temp_contactsmember = hst->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
		if (temp_contactsmember->contact_ptr == cntct)
			return TRUE;
	}

	/* search all contactgroups of this host */
	for (temp_contactgroupsmember = hst->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
		temp_contactgroup = temp_contactgroupsmember->group_ptr;
		if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
			return TRUE;
	}

	return FALSE;
}

/* tests whether or not a contact is an escalated contact for a particular host */
int is_escalated_contact_for_host(host *hst, contact *cntct)
{
	contactsmember *temp_contactsmember = NULL;
	hostescalation *temp_hostescalation = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;
	objectlist *list;

	/* search all host escalations */
	for (list = hst->escalation_list; list; list = list->next) {
		temp_hostescalation = (hostescalation *)list->object_ptr;

		/* search all contacts of this host escalation */
		for (temp_contactsmember = temp_hostescalation->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
			if (temp_contactsmember->contact_ptr == cntct)
				return TRUE;
		}

		/* search all contactgroups of this host escalation */
		for (temp_contactgroupsmember = temp_hostescalation->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
			temp_contactgroup = temp_contactgroupsmember->group_ptr;
			if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
				return TRUE;
		}
	}

	return FALSE;
}

/* Host/Service dependencies are not visible in Nagios CGIs, so we exclude them */
unsigned int host_services_value(host *h)
{
	servicesmember *sm;
	unsigned int ret = 0;
	for (sm = h->services; sm; sm = sm->next) {
		ret += sm->service_ptr->hourly_value;
	}
	return ret;
}

struct implode_parameters {
	char *delimiter;
	GString *buf;
};

static gboolean implode_helper(gpointer _name, gpointer _hst, gpointer user_data)
{
	host *hst = (host *)_hst;
	struct implode_parameters *params = (struct implode_parameters *)user_data;
	if (params->buf->len > 0)
		g_string_append(params->buf, params->delimiter);
	g_string_append(params->buf, hst->name);

	return FALSE;
}

char *implode_hosttree(GTree *tree, char *delimiter)
{
	char *result;
	struct implode_parameters params;
	params.delimiter = delimiter;
	params.buf = g_string_new("");
	g_tree_foreach(tree, implode_helper, &params);
	result = malloc(params.buf->len + 1);
	strncpy(result, params.buf->str, params.buf->len);
	result[params.buf->len] = 0;
	g_string_free(params.buf, TRUE);
	return result;
}

void fcache_host(FILE *fp, const host *temp_host)
{
	fprintf(fp, "define host {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_host->name);
	if (temp_host->display_name != temp_host->name)
		fprintf(fp, "\tdisplay_name\t%s\n", temp_host->display_name);
	if (temp_host->alias)
		fprintf(fp, "\talias\t%s\n", temp_host->alias);
	if (temp_host->address)
		fprintf(fp, "\taddress\t%s\n", temp_host->address);
	if (g_tree_nnodes(temp_host->parent_hosts) > 0) {
		char *parents;
		parents = implode_hosttree(temp_host->parent_hosts, ",");
		fprintf(fp, "\tparents\t%s\n", parents);
		nm_free(parents);
	}
	if (temp_host->check_period)
		fprintf(fp, "\tcheck_period\t%s\n", temp_host->check_period);
	if (temp_host->check_command)
		fprintf(fp, "\tcheck_command\t%s\n", temp_host->check_command);
	if (temp_host->event_handler)
		fprintf(fp, "\tevent_handler\t%s\n", temp_host->event_handler);
	fcache_contactlist(fp, "\tcontacts\t", temp_host->contacts);
	fcache_contactgrouplist(fp, "\tcontact_groups\t", temp_host->contact_groups);
	if (temp_host->notification_period)
		fprintf(fp, "\tnotification_period\t%s\n", temp_host->notification_period);
	fprintf(fp, "\tinitial_state\t");
	if (temp_host->initial_state == STATE_DOWN)
		fprintf(fp, "d\n");
	else if (temp_host->initial_state == STATE_UNREACHABLE)
		fprintf(fp, "u\n");
	else
		fprintf(fp, "o\n");
	fprintf(fp, "\thourly_value\t%u\n", temp_host->hourly_value);
	fprintf(fp, "\tcheck_interval\t%f\n", temp_host->check_interval);
	fprintf(fp, "\tretry_interval\t%f\n", temp_host->retry_interval);
	fprintf(fp, "\tmax_check_attempts\t%d\n", temp_host->max_attempts);
	fprintf(fp, "\tactive_checks_enabled\t%d\n", temp_host->checks_enabled);
	fprintf(fp, "\tpassive_checks_enabled\t%d\n", temp_host->accept_passive_checks);
	fprintf(fp, "\tobsess\t%d\n", temp_host->obsess);
	fprintf(fp, "\tevent_handler_enabled\t%d\n", temp_host->event_handler_enabled);
	fprintf(fp, "\tlow_flap_threshold\t%f\n", temp_host->low_flap_threshold);
	fprintf(fp, "\thigh_flap_threshold\t%f\n", temp_host->high_flap_threshold);
	fprintf(fp, "\tflap_detection_enabled\t%d\n", temp_host->flap_detection_enabled);
	fprintf(fp, "\tflap_detection_options\t%s\n", opts2str(temp_host->flap_detection_options, host_flag_map, 'o'));
	fprintf(fp, "\tfreshness_threshold\t%d\n", temp_host->freshness_threshold);
	fprintf(fp, "\tcheck_freshness\t%d\n", temp_host->check_freshness);
	fprintf(fp, "\tnotification_options\t%s\n", opts2str(temp_host->notification_options, host_flag_map, 'r'));
	fprintf(fp, "\tnotifications_enabled\t%d\n", temp_host->notifications_enabled);
	fprintf(fp, "\tnotification_interval\t%f\n", temp_host->notification_interval);
	fprintf(fp, "\tfirst_notification_delay\t%f\n", temp_host->first_notification_delay);
	fprintf(fp, "\tstalking_options\t%s\n", opts2str(temp_host->stalking_options, host_flag_map, 'o'));
	fprintf(fp, "\tprocess_perf_data\t%d\n", temp_host->process_performance_data);
	if (temp_host->icon_image)
		fprintf(fp, "\ticon_image\t%s\n", temp_host->icon_image);
	if (temp_host->icon_image_alt)
		fprintf(fp, "\ticon_image_alt\t%s\n", temp_host->icon_image_alt);
	if (temp_host->vrml_image)
		fprintf(fp, "\tvrml_image\t%s\n", temp_host->vrml_image);
	if (temp_host->statusmap_image)
		fprintf(fp, "\tstatusmap_image\t%s\n", temp_host->statusmap_image);
	if (temp_host->have_2d_coords == TRUE)
		fprintf(fp, "\t2d_coords\t%d,%d\n", temp_host->x_2d, temp_host->y_2d);
	if (temp_host->have_3d_coords == TRUE)
		fprintf(fp, "\t3d_coords\t%f,%f,%f\n", temp_host->x_3d, temp_host->y_3d, temp_host->z_3d);
	if (temp_host->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_host->notes);
	if (temp_host->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_host->notes_url);
	if (temp_host->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_host->action_url);
	fprintf(fp, "\tretain_status_information\t%d\n", temp_host->retain_status_information);
	fprintf(fp, "\tretain_nonstatus_information\t%d\n", temp_host->retain_nonstatus_information);

	/* custom variables */
	fcache_customvars(fp, temp_host->custom_variables);
	fprintf(fp, "\t}\n\n");
}

/* write a host problem/recovery to the log file */
int log_host_event(host *hst)
{
	unsigned long log_options = 0L;

	/* get the log options */
	if (hst->current_state == STATE_DOWN)
		log_options = NSLOG_HOST_DOWN;
	else if (hst->current_state == STATE_UNREACHABLE)
		log_options = NSLOG_HOST_UNREACHABLE;
	else
		log_options = NSLOG_HOST_UP;

	nm_log(log_options, "HOST ALERT: %s;%s;%s;%d;%s\n",
	       hst->name,
	       host_state_name(hst->current_state),
	       state_type_name(hst->state_type),
	       hst->current_attempt,
	       (hst->plugin_output == NULL) ? "" : hst->plugin_output);

	return OK;
}


/* logs host states */
int log_host_states(int type, time_t *timestamp)
{
	host *temp_host = NULL;;

	/* bail if we shouldn't be logging initial states */
	if (type == INITIAL_STATES && log_initial_states == FALSE)
		return OK;

	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {
		nm_log(NSLOG_INFO_MESSAGE, "%s HOST STATE: %s;%s;%s;%d;%s\n", (type == INITIAL_STATES) ? "INITIAL" : "CURRENT",
		       temp_host->name,
		       host_state_name(temp_host->current_state),
		       state_type_name(temp_host->state_type),
		       temp_host->current_attempt,
		       (temp_host->plugin_output == NULL) ? "" : temp_host->plugin_output);
	}

	return OK;
}
