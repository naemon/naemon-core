#include "objects_service.h"
#include "objects_serviceescalation.h"
#include "objects_servicedependency.h"
#include "objects_host.h"
#include "objects_timeperiod.h"
#include "objects_contactgroup.h"
#include "objects_contact.h"
#include "objects_common.h"
#include "nm_alloc.h"
#include "logging.h"
#include "objects_fcache.h"
#include "globals.h"
#include <glib.h>
#include "lib/libnaemon.h"

static GHashTable *service_hash_table;
service *service_list = NULL;
service **service_ary = NULL;

int init_objects_service(int elems)
{
	service_ary = nm_calloc(elems, sizeof(service *));
	service_hash_table = g_hash_table_new_full(nm_service_hash, nm_service_equal,
	                     (GDestroyNotify) nm_service_key_destroy, NULL);
	return OK;
}

/* destroy a single service object, set truncate_lists to TRUE when lists should be simply emptied instead of removing item by item.
 * Enable truncate_list when removing all objects and disable when removing a specific one. */
void destroy_objects_service(int truncate_lists)
{
	unsigned int i;
	for (i = 0; i < num_objects.services; i++) {
		service *this_service = service_ary[i];
		destroy_service(this_service, truncate_lists);
	}
	service_list = NULL;
	if (service_hash_table)
		g_hash_table_destroy(service_hash_table);

	service_hash_table = NULL;
	nm_free(service_ary);
	num_objects.services = 0;
}

service *create_service(host *hst, const char *description)
{
	service *new_service = NULL;
	servicesmember *new_servicesmember = NULL;

	if (!hst) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: No host provided for service '%s'\n",
		       description);
		return NULL;
	}

	if (description == NULL || !*description) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Found service on host '%s' with no service description\n", hst->name);
		return NULL;
	}

	if (contains_illegal_object_chars(description) == TRUE) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: The description string for service '%s' on host '%s' contains one or more illegal characters.", description, hst->name);
		return NULL;
	}

	/* allocate memory */
	new_service = nm_calloc(1, sizeof(*new_service));

	new_service->host_ptr = hst;
	new_service->host_name = hst->name;

	new_servicesmember = nm_calloc(1, sizeof(servicesmember));
	new_servicesmember->host_name = new_service->host_name;
	new_servicesmember->service_description = new_service->description;
	new_servicesmember->service_ptr = new_service;
	new_servicesmember->next = hst->services;
	hst->services = new_servicesmember;
	hst->total_services++;

	new_service->description = nm_strdup(description);
	new_service->display_name = new_service->description;
	new_service->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
	new_service->acknowledgement_end_time = (time_t)0;
	new_service->check_type = CHECK_TYPE_ACTIVE;
	new_service->state_type = HARD_STATE;
	new_service->check_options = CHECK_OPTION_NONE;

	return new_service;
}

int setup_service_variables(service *new_service, const char *display_name, const char *check_command, const char *check_period, int initial_state, int check_timeout, int max_attempts, int accept_passive_checks, double check_interval, double retry_interval, double notification_interval, double first_notification_delay, char *notification_period, int notification_options, int notifications_enabled, int is_volatile, const char *event_handler, int event_handler_enabled, int checks_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, const char *notes, const char *notes_url, const char *action_url, const char *icon_image, const char *icon_image_alt, int retain_status_information, int retain_nonstatus_information, int obsess, unsigned int hourly_value)
{
	timeperiod *cp = NULL, *np = NULL;
	command *cmd;

	/* make sure we have everything we need */
	if (notification_period && !(np = find_timeperiod(notification_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: notification_period '%s' for service '%s' on host '%s' could not be found!\n", notification_period, new_service->description, new_service->host_name);
		return -1;
	}
	if (check_period && !(cp = find_timeperiod(check_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: check_period '%s' for service '%s' on host '%s' not found!\n",
		       check_period, new_service->description, new_service->host_name);
		return -1;
	}

	if (check_command == NULL || !*check_command) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: No check command provided for service '%s' on host '%s'\n", new_service->check_command, new_service->description);
		return -1;
	}
	cmd = find_bang_command(check_command);
	if (cmd == NULL) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Service check command '%s' specified in service '%s' for host '%s' not defined anywhere!", check_command, new_service->description, new_service->host_name);
		return -1;
	}


	if (max_attempts <= 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: max_check_attempts must be a positive integer for service '%s' on host '%s'\n", new_service->description, new_service->host_name);
		return -1;
	}
	if (check_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: check_interval must be a non-negative integer for service '%s' on host '%s'\n", new_service->description, new_service->host_name);
		return -1;
	}
	if (check_timeout <= 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: check_timeout must be a positive integer for service '%s' on host '%s'\n", new_service->description, new_service->host_name);
		return -1;
	}
	if (retry_interval <= 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: retry_interval must be a positive integer for service '%s' on host '%s'\n", new_service->description, new_service->host_name);
		return -1;
	}
	if (notification_interval < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: notification_interval must be a non-negative integer for service '%s' on host '%s'\n", new_service->description, new_service->host_name);
		return -1;
	}
	if (first_notification_delay < 0) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: first_notification_delay must be a non-negative integer for service '%s' on host '%s'\n", new_service->description, new_service->host_name);
		return -1;
	}

	/* duplicate vars, but assign what we can */
	new_service->notification_period_ptr = np;
	new_service->check_period_ptr = cp;
	new_service->check_period = cp ? nm_strdup(cp->name) : NULL;
	new_service->notification_period = np ? nm_strdup(np->name) : NULL;
	new_service->check_command = nm_strdup(check_command);
	new_service->check_command_ptr = cmd;
	if (display_name) {
		new_service->display_name = nm_strdup(display_name);
	}
	if (event_handler) {
		new_service->event_handler = nm_strdup(event_handler);
		new_service->event_handler_ptr = find_bang_command(event_handler);
		if (new_service->event_handler_ptr == NULL) {
			nm_log(NSLOG_VERIFICATION_ERROR, "Error: Event handler command '%s' specified in service '%s' for host '%s' not defined anywhere", new_service->event_handler, new_service->description, new_service->host_name);
			return -1;
		}
	}
	if (notes) {
		new_service->notes = nm_strdup(notes);
	}
	if (notes_url) {
		new_service->notes_url = nm_strdup(notes_url);
	}
	if (action_url) {
		new_service->action_url = nm_strdup(action_url);
	}
	if (icon_image) {
		new_service->icon_image = nm_strdup(icon_image);
	}
	if (icon_image_alt) {
		new_service->icon_image_alt = nm_strdup(icon_image_alt);
	}

	new_service->hourly_value = hourly_value;
	new_service->check_timeout = check_timeout;
	new_service->check_interval = check_interval;
	new_service->retry_interval = retry_interval;
	new_service->max_attempts = max_attempts;
	new_service->notification_interval = notification_interval;
	new_service->first_notification_delay = first_notification_delay;
	new_service->notification_options = notification_options;
	new_service->is_volatile = (is_volatile > 0) ? TRUE : FALSE;
	new_service->flap_detection_enabled = (flap_detection_enabled > 0) ? TRUE : FALSE;
	new_service->low_flap_threshold = low_flap_threshold;
	new_service->high_flap_threshold = high_flap_threshold;
	new_service->flap_detection_options = flap_detection_options;
	new_service->stalking_options = stalking_options;
	new_service->process_performance_data = (process_perfdata > 0) ? TRUE : FALSE;
	new_service->check_freshness = (check_freshness > 0) ? TRUE : FALSE;
	new_service->freshness_threshold = freshness_threshold;
	new_service->accept_passive_checks = (accept_passive_checks > 0) ? TRUE : FALSE;
	new_service->event_handler_enabled = (event_handler_enabled > 0) ? TRUE : FALSE;
	new_service->checks_enabled = (checks_enabled > 0) ? TRUE : FALSE;
	new_service->retain_status_information = (retain_status_information > 0) ? TRUE : FALSE;
	new_service->retain_nonstatus_information = (retain_nonstatus_information > 0) ? TRUE : FALSE;
	new_service->notifications_enabled = (notifications_enabled > 0) ? TRUE : FALSE;
	new_service->obsess = (obsess > 0) ? TRUE : FALSE;
	new_service->current_attempt = (initial_state == STATE_OK) ? 1 : max_attempts;
	new_service->current_state = initial_state;
	new_service->last_state = initial_state;
	new_service->last_hard_state = initial_state;

	/* check the service check_command */

	return 0;
}

int register_service(service *new_service)
{

	host *h;
	g_return_val_if_fail(service_hash_table != NULL, ERROR);

	if (!(h = find_host(new_service->host_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Unable to locate host '%s' for service '%s'\n",
		       new_service->host_name, new_service->description);
		return ERROR;
	}

	if ((find_service(new_service->host_name, new_service->description))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Service '%s' on host '%s' has already been defined\n", new_service->description, new_service->host_name);
		return ERROR;
	}

	g_hash_table_insert(service_hash_table,
	                    nm_service_key_create(new_service->host_name, new_service->description), new_service);

	new_service->id = num_objects.services++;
	service_ary[new_service->id] = new_service;
	if (new_service->id)
		service_ary[new_service->id - 1]->next = new_service;
	else
		service_list = new_service;

	return OK;
}

servicesmember *add_parent_to_service(service *svc, service *parent)
{
	servicesmember *sm;

	if (!svc || !parent)
		return NULL;

	sm = nm_calloc(1, sizeof(*sm));

	sm->host_name = parent->host_name;
	sm->service_description = parent->description;
	sm->service_ptr = parent;
	sm->next = svc->parents;
	svc->parents = sm;

	return sm;
}

contactgroupsmember *add_contactgroup_to_service(service *svc, char *group_name)
{
	return add_contactgroup_to_object(&svc->contact_groups, group_name);
}

contactsmember *add_contact_to_service(service *svc, char *contact_name)
{
	return add_contact_to_object(&svc->contacts, contact_name);
}

customvariablesmember *add_custom_variable_to_service(service *svc, char *varname, char *varvalue)
{
	return add_custom_variable_to_object(&svc->custom_variables, varname, varvalue);
}

/* destroy a single service object, set truncate_lists to TRUE when lists should be simply emptied instead of removing item by item.
 * Enable truncate_list when removing all objects and disble when removing a specific one. */
void destroy_service(service *this_service, int truncate_lists)
{
	struct contactgroupsmember *this_contactgroupsmember, *next_contactgroupsmember;
	struct contactsmember *this_contactsmember, *next_contactsmember;
	struct customvariablesmember *this_customvariablesmember, *next_customvariablesmember;
	struct servicesmember *this_servicesmember, *next_servicesmember;
	struct objectlist *slavelist;

	if (!this_service)
		return;

	/* free memory for contact groups */
	this_contactgroupsmember = this_service->contact_groups;
	while (this_contactgroupsmember != NULL) {
		next_contactgroupsmember = this_contactgroupsmember->next;
		nm_free(this_contactgroupsmember);
		this_contactgroupsmember = next_contactgroupsmember;
	}

	/* free memory for contacts */
	this_contactsmember = this_service->contacts;
	while (this_contactsmember != NULL) {
		next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}

	/* free memory for custom variables */
	this_customvariablesmember = this_service->custom_variables;
	while (this_customvariablesmember != NULL) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
		this_customvariablesmember = next_customvariablesmember;
	}

	/* free memory for service parents */
	this_servicesmember = this_service->parents;
	while (this_servicesmember != NULL) {
		next_servicesmember = this_servicesmember->next;
		nm_free(this_servicesmember);
		this_servicesmember = next_servicesmember;
	}

	/* free memory for service groups */
	if(!truncate_lists) {
		/* remove them one by one */
		while (this_service->servicegroups_ptr)
			remove_service_from_servicegroup(this_service->servicegroups_ptr->object_ptr, this_service);
	}

	for (slavelist = this_service->notify_deps; slavelist; slavelist = slavelist->next)
		destroy_servicedependency(slavelist->object_ptr);
	for (slavelist = this_service->exec_deps; slavelist; slavelist = slavelist->next)
		destroy_servicedependency(slavelist->object_ptr);
	for (slavelist = this_service->escalation_list; slavelist; slavelist = slavelist->next)
		destroy_serviceescalation(slavelist->object_ptr);

	if (this_service->display_name != this_service->description)
		nm_free(this_service->display_name);
	nm_free(this_service->description);
	nm_free(this_service->check_command);
	nm_free(this_service->plugin_output);
	nm_free(this_service->long_plugin_output);
	nm_free(this_service->perf_data);
	nm_free(this_service->event_handler_args);
	free_objectlist(&this_service->comments_list);
	free_objectlist(&this_service->servicegroups_ptr);
	free_objectlist(&this_service->notify_deps);
	free_objectlist(&this_service->exec_deps);
	free_objectlist(&this_service->escalation_list);
	nm_free(this_service->event_handler);
	nm_free(this_service->check_period);
	nm_free(this_service->notification_period);
	nm_free(this_service->notes);
	nm_free(this_service->notes_url);
	nm_free(this_service->action_url);
	nm_free(this_service->icon_image);
	nm_free(this_service->icon_image_alt);
	nm_free(this_service->current_notification_id);
	nm_free(this_service->last_problem_id);
	nm_free(this_service->current_problem_id);
	nm_free(this_service);
}

service *find_service(const char *host_name, const char *svc_desc)
{
	if (!host_name || !svc_desc)
		return NULL;

	return g_hash_table_lookup(service_hash_table, &((nm_service_key) {
		(char *)host_name, (char *)svc_desc
	}));
}

int get_service_count(void)
{
	return num_objects.services;
}

time_t get_service_check_interval_s(const service *svc)
{
	return svc->check_interval * interval_length;
}

time_t get_service_retry_interval_s(const service *svc)
{
	return svc->retry_interval * interval_length;
}

const char *service_state_name(int state)
{
	switch (state) {
	case STATE_OK: return "OK";
	case STATE_WARNING: return "WARNING";
	case STATE_CRITICAL: return "CRITICAL";
	}

	return "UNKNOWN";
}

int is_contact_for_service(service *svc, contact *cntct)
{
	contactsmember *temp_contactsmember = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;

	if (svc == NULL || cntct == NULL)
		return FALSE;

	/* search all individual contacts of this service */
	for (temp_contactsmember = svc->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
		if (temp_contactsmember->contact_ptr == cntct)
			return TRUE;
	}

	/* search all contactgroups of this service */
	for (temp_contactgroupsmember = svc->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
		temp_contactgroup = temp_contactgroupsmember->group_ptr;
		if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
			return TRUE;

	}

	return FALSE;
}

/* tests whether or not a contact is an escalated contact for a particular service */
int is_escalated_contact_for_service(service *svc, contact *cntct)
{
	serviceescalation *temp_serviceescalation = NULL;
	contactsmember *temp_contactsmember = NULL;
	contactgroupsmember *temp_contactgroupsmember = NULL;
	contactgroup *temp_contactgroup = NULL;
	objectlist *list;

	/* search all the service escalations */
	for (list = svc->escalation_list; list; list = list->next) {
		temp_serviceescalation = (serviceescalation *)list->object_ptr;

		/* search all contacts of this service escalation */
		for (temp_contactsmember = temp_serviceescalation->contacts; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
			if (temp_contactsmember->contact_ptr == cntct)
				return TRUE;
		}

		/* search all contactgroups of this service escalation */
		for (temp_contactgroupsmember = temp_serviceescalation->contact_groups; temp_contactgroupsmember != NULL; temp_contactgroupsmember = temp_contactgroupsmember->next) {
			temp_contactgroup = temp_contactgroupsmember->group_ptr;
			if (is_contact_member_of_contactgroup(temp_contactgroup, cntct))
				return TRUE;
		}
	}

	return FALSE;
}

void nm_fcache_service(struct nm_writebuf *wb, const service *temp_service)
{
	nm_wb_lit(wb, "define service {\n");
	fc_str(wb, "host_name", temp_service->host_name);
	fc_str(wb, "service_description", temp_service->description);
	if (temp_service->display_name != temp_service->description)
		fc_str(wb, "display_name", temp_service->display_name);
	if (temp_service->parents) {
		nm_wb_lit(wb, "\tparents\t");
		/* same-host, single-parent? */
		if (!temp_service->parents->next && temp_service->parents->service_ptr->host_ptr == temp_service->host_ptr) {
			fc_s(wb, temp_service->parents->service_ptr->description);
			nm_wb_lit(wb, "\n");
		} else {
			servicesmember *sm;
			for (sm = temp_service->parents; sm; sm = sm->next) {
				fc_s(wb, sm->host_name);
				nm_wb_lit(wb, ",");
				fc_s(wb, sm->service_description);
				fc_sep(wb, sm->next != NULL);
			}
		}
	}
	if (temp_service->check_period)
		fc_str(wb, "check_period", temp_service->check_period);
	if (temp_service->check_command)
		fc_str(wb, "check_command", temp_service->check_command);
	if (temp_service->event_handler)
		fc_str(wb, "event_handler", temp_service->event_handler);
	nm_fcache_contactlist(wb, "\tcontacts\t", temp_service->contacts);
	nm_fcache_contactgrouplist(wb, "\tcontact_groups\t", temp_service->contact_groups);
	if (temp_service->notification_period)
		fc_str(wb, "notification_period", temp_service->notification_period);
	if (temp_service->initial_state == STATE_WARNING)
		nm_wb_lit(wb, "\tinitial_state\tw\n");
	else if (temp_service->initial_state == STATE_UNKNOWN)
		nm_wb_lit(wb, "\tinitial_state\tu\n");
	else if (temp_service->initial_state == STATE_CRITICAL)
		nm_wb_lit(wb, "\tinitial_state\tc\n");
	else
		nm_wb_lit(wb, "\tinitial_state\to\n");
	fc_int(wb, "check_timeout", temp_service->check_timeout);
	fc_uint(wb, "hourly_value", temp_service->hourly_value);
	fc_dbl(wb, "check_interval", temp_service->check_interval);
	fc_dbl(wb, "retry_interval", temp_service->retry_interval);
	fc_int(wb, "max_check_attempts", temp_service->max_attempts);
	fc_int(wb, "is_volatile", temp_service->is_volatile);
	fc_int(wb, "active_checks_enabled", temp_service->checks_enabled);
	fc_int(wb, "passive_checks_enabled", temp_service->accept_passive_checks);
	fc_int(wb, "obsess", temp_service->obsess);
	fc_int(wb, "event_handler_enabled", temp_service->event_handler_enabled);
	fc_dbl(wb, "low_flap_threshold", temp_service->low_flap_threshold);
	fc_dbl(wb, "high_flap_threshold", temp_service->high_flap_threshold);
	fc_int(wb, "flap_detection_enabled", temp_service->flap_detection_enabled);
	fc_str(wb, "flap_detection_options", opts2str(temp_service->flap_detection_options, service_flag_map, 'o'));
	fc_int(wb, "freshness_threshold", temp_service->freshness_threshold);
	fc_int(wb, "check_freshness", temp_service->check_freshness);
	fc_str(wb, "notification_options", opts2str(temp_service->notification_options, service_flag_map, 'r'));
	fc_int(wb, "notifications_enabled", temp_service->notifications_enabled);
	fc_dbl(wb, "notification_interval", temp_service->notification_interval);
	fc_dbl(wb, "first_notification_delay", temp_service->first_notification_delay);
	fc_str(wb, "stalking_options", opts2str(temp_service->stalking_options, service_flag_map, 'o'));
	fc_int(wb, "process_perf_data", temp_service->process_performance_data);
	if (temp_service->icon_image)
		fc_str(wb, "icon_image", temp_service->icon_image);
	if (temp_service->icon_image_alt)
		fc_str(wb, "icon_image_alt", temp_service->icon_image_alt);
	if (temp_service->notes)
		fc_str(wb, "notes", temp_service->notes);
	if (temp_service->notes_url)
		fc_str(wb, "notes_url", temp_service->notes_url);
	if (temp_service->action_url)
		fc_str(wb, "action_url", temp_service->action_url);
	fc_int(wb, "retain_status_information", temp_service->retain_status_information);
	fc_int(wb, "retain_nonstatus_information", temp_service->retain_nonstatus_information);

	/* custom variables */
	nm_fcache_customvars(wb, temp_service->custom_variables);
	nm_wb_lit(wb, "\t}\n\n");
}

void fcache_service(FILE *fp, const service *temp_service)
{
	struct nm_writebuf wb;

	fc_file_begin(&wb, fp);
	nm_fcache_service(&wb, temp_service);
	nm_writebuf_done(&wb);
}

/* write a service problem/recovery to the naemon log file */
int log_service_event(service *svc)
{
	unsigned long log_options = 0L;

	/* don't log soft errors if the user doesn't want to */
	if (svc->state_type == SOFT_STATE && !log_service_retries)
		return OK;

	/* get the log options */
	if (svc->current_state == STATE_UNKNOWN)
		log_options = NSLOG_SERVICE_UNKNOWN;
	else if (svc->current_state == STATE_WARNING)
		log_options = NSLOG_SERVICE_WARNING;
	else if (svc->current_state == STATE_CRITICAL)
		log_options = NSLOG_SERVICE_CRITICAL;
	else
		log_options = NSLOG_SERVICE_OK;

	nm_log(log_options, "SERVICE ALERT: %s;%s;%s;%s;%d;%s",
	       svc->host_name, svc->description,
	       service_state_name(svc->current_state),
	       state_type_name(svc->state_type),
	       svc->current_attempt,
	       (svc->plugin_output == NULL) ? "" : svc->plugin_output);

	return OK;
}


/* logs service states */
int log_service_states(int type, time_t *timestamp)
{
	service *temp_service = NULL;

	/* bail if we shouldn't be logging initial states */
	if (type == INITIAL_STATES && log_initial_states == FALSE)
		return OK;

	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		nm_log(type, "%s SERVICE STATE: %s;%s;%s;%s;%d;%s",
		       (type == INITIAL_STATES) ? "INITIAL" : "CURRENT",
		       temp_service->host_name, temp_service->description,
		       service_state_name(temp_service->current_state),
		       state_type_name(temp_service->state_type),
		       temp_service->current_attempt,
		       temp_service->plugin_output);
	}

	return OK;
}
