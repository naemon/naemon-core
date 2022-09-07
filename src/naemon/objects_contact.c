#include "objects_contact.h"
#include "objects_service.h"
#include "objects_host.h"
#include "objects_timeperiod.h"
#include "objects_command.h"
#include "objectlist.h"
#include "xodtemplate.h"
#include "logging.h"
#include "nm_alloc.h"
#include <string.h>
#include <glib.h>

static GHashTable *contact_hash_table = NULL;
contact *contact_list = NULL;
contact **contact_ary = NULL;

int init_objects_contact(int elems)
{
	contact_ary = nm_calloc(elems, sizeof(contact *));
	contact_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	return OK;
}

void destroy_objects_contact()
{
	unsigned int i;
	for (i = 0; i < num_objects.contacts; i++) {
		contact *this_contact = contact_ary[i];
		destroy_contact(this_contact);
	}
	contact_list = NULL;
	if (contact_hash_table)
		g_hash_table_destroy(contact_hash_table);

	contact_hash_table = NULL;
	nm_free(contact_ary);
	num_objects.contacts = 0;
}

contact *create_contact(const char *name)
{
	contact *new_contact = NULL;

	if (name == NULL || !*name) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact name is NULL\n");
		return NULL;
	}
	if (contains_illegal_object_chars(name) == TRUE) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: The name of contact '%s' contains one or more illegal characters.", name);
		return NULL;
	}
	new_contact = nm_calloc(1, sizeof(*new_contact));
	new_contact->name = nm_strdup(name);
	new_contact->alias = new_contact->name;
	return new_contact;
}

int setup_contact_variables(contact *new_contact, const char *alias, const char *email, const char *pager, char *const *addresses, const char *svc_notification_period, const char *host_notification_period, int service_notification_options, int host_notification_options, int host_notifications_enabled, int service_notifications_enabled, int can_submit_commands, int retain_status_information, int retain_nonstatus_information, unsigned int minimum_value)
{
	timeperiod *htp = NULL, *stp = NULL;
	int x = 0;

	if (svc_notification_period && !(stp = find_timeperiod(svc_notification_period))) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Service notification period '%s' specified for contact '%s' is not defined anywhere!\n",
		       svc_notification_period, new_contact->name);
		return -1;
	}
	if (host_notification_period && !(htp = find_timeperiod(host_notification_period))) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Host notification period '%s' specified for contact '%s' is not defined anywhere!\n",
		       host_notification_period, new_contact->name);
		return -1;
	}

	new_contact->host_notification_period = htp ? nm_strdup(htp->name) : NULL;
	new_contact->service_notification_period = stp ? nm_strdup(stp->name) : NULL;
	new_contact->host_notification_period_ptr = htp;
	new_contact->service_notification_period_ptr = stp;
	if (alias)
		new_contact->alias = nm_strdup(alias);
	new_contact->email = email ? nm_strdup(email) : NULL;
	new_contact->pager = pager ? nm_strdup(pager) : NULL;
	if (addresses) {
		for (x = 0; x < MAX_CONTACT_ADDRESSES; x++)
			new_contact->address[x] = addresses[x] ? nm_strdup(addresses[x]) : NULL;
	}

	new_contact->minimum_value = minimum_value;
	new_contact->service_notification_options = service_notification_options;
	new_contact->host_notification_options = host_notification_options;
	new_contact->host_notifications_enabled = (host_notifications_enabled > 0) ? TRUE : FALSE;
	new_contact->service_notifications_enabled = (service_notifications_enabled > 0) ? TRUE : FALSE;
	new_contact->can_submit_commands = (can_submit_commands > 0) ? TRUE : FALSE;
	new_contact->retain_status_information = (retain_status_information > 0) ? TRUE : FALSE;
	new_contact->retain_nonstatus_information = (retain_nonstatus_information > 0) ? TRUE : FALSE;

	return 0;
}

int register_contact(contact *new_contact)
{

	g_return_val_if_fail(contact_hash_table != NULL, ERROR);

	if ((find_contact(new_contact->name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact '%s' has already been defined\n", new_contact->name);
		return ERROR;
	}

	g_hash_table_insert(contact_hash_table, new_contact->name, new_contact);

	new_contact->id = num_objects.contacts++;
	contact_ary[new_contact->id] = new_contact;
	if (new_contact->id)
		contact_ary[new_contact->id - 1]->next = new_contact;
	else
		contact_list = new_contact;

	return OK;
}

void destroy_contact(contact *this_contact)
{
	int j;
	commandsmember *this_commandsmember;
	customvariablesmember *this_customvariablesmember;

	if (!this_contact)
		return;

	/* free memory for the host notification commands */
	this_commandsmember = this_contact->host_notification_commands;
	while (this_commandsmember != NULL) {
		commandsmember *next_commandsmember = this_commandsmember->next;
		if (this_commandsmember->command != NULL)
			nm_free(this_commandsmember->command);
		nm_free(this_commandsmember);
		this_commandsmember = next_commandsmember;
	}

	/* free memory for the service notification commands */
	this_commandsmember = this_contact->service_notification_commands;
	while (this_commandsmember != NULL) {
		commandsmember *next_commandsmember = this_commandsmember->next;
		if (this_commandsmember->command != NULL)
			nm_free(this_commandsmember->command);
		nm_free(this_commandsmember);
		this_commandsmember = next_commandsmember;
	}

	/* free memory for custom variables */
	this_customvariablesmember = this_contact->custom_variables;
	while (this_customvariablesmember != NULL) {
		customvariablesmember *next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
		this_customvariablesmember = next_customvariablesmember;
	}

	if (this_contact->alias != this_contact->name)
		nm_free(this_contact->alias);
	nm_free(this_contact->name);
	nm_free(this_contact->email);
	nm_free(this_contact->pager);
	for (j = 0; j < MAX_CONTACT_ADDRESSES; j++)
		nm_free(this_contact->address[j]);

	free_objectlist(&this_contact->contactgroups_ptr);
	nm_free(this_contact);
}

/* adds a host notification command to a contact definition */
commandsmember *add_host_notification_command_to_contact(contact *cntct, char *command_name)
{
	commandsmember *new_commandsmember = NULL;
	command *cmd;

	/* make sure we have the data we need */
	if (cntct == NULL || (command_name == NULL || !strcmp(command_name, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact or host notification command is NULL\n");
		return NULL;
	}

	cmd = find_bang_command(command_name);
	if (cmd == NULL) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Host notification command '%s' specified for contact '%s' is not defined anywhere!", command_name, cntct->name);
		return NULL;
	}

	/* allocate memory */
	new_commandsmember = nm_calloc(1, sizeof(commandsmember));

	/* duplicate vars */
	new_commandsmember->command = nm_strdup(command_name);
	new_commandsmember->command_ptr = cmd;

	/* add the notification command */
	new_commandsmember->next = cntct->host_notification_commands;
	cntct->host_notification_commands = new_commandsmember;

	return new_commandsmember;
}


/* adds a service notification command to a contact definition */
commandsmember *add_service_notification_command_to_contact(contact *cntct, char *command_name)
{
	commandsmember *new_commandsmember = NULL;
	command *cmd = NULL;

	/* make sure we have the data we need */
	if (cntct == NULL || (command_name == NULL || !strcmp(command_name, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact or service notification command is NULL\n");
		return NULL;
	}

	cmd = find_bang_command(command_name);
	if (cmd == NULL) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Service notification command '%s' specified for contact '%s' is not defined anywhere!", command_name, cntct->name);
		return NULL;
	}

	/* allocate memory */
	new_commandsmember = nm_calloc(1, sizeof(commandsmember));

	/* duplicate vars */
	new_commandsmember->command = nm_strdup(command_name);
	new_commandsmember->command_ptr = cmd;

	/* add the notification command */
	new_commandsmember->next = cntct->service_notification_commands;
	cntct->service_notification_commands = new_commandsmember;

	return new_commandsmember;
}


/* adds a custom variable to a contact */
customvariablesmember *add_custom_variable_to_contact(contact *cntct, char *varname, char *varvalue)
{

	return add_custom_variable_to_object(&cntct->custom_variables, varname, varvalue);
}


contactsmember *add_contact_to_object(contactsmember **object_ptr, char *contactname)
{
	contactsmember *new_contactsmember = NULL;
	contact *c;

	/* make sure we have the data we need */
	if (object_ptr == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact object is NULL\n");
		return NULL;
	}

	if (contactname == NULL || !*contactname) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact name is NULL\n");
		return NULL;
	}
	if (!(c = find_contact(contactname))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact '%s' is not defined anywhere!\n", contactname);
		return NULL;
	}

	/* allocate memory for a new member */
	new_contactsmember = nm_malloc(sizeof(contactsmember));
	new_contactsmember->contact_name = c->name;

	/* set initial values */
	new_contactsmember->contact_ptr = c;

	/* add the new contact to the head of the contact list */
	new_contactsmember->next = *object_ptr;
	*object_ptr = new_contactsmember;

	return new_contactsmember;
}

contact *find_contact(const char *name)
{
	return name ? g_hash_table_lookup(contact_hash_table, name) : NULL;
}

void fcache_contactlist(FILE *fp, const char *prefix, const contactsmember *list)
{
	if (list) {
		contactsmember const *l;
		fprintf(fp, "%s", prefix);
		for (l = list; l; l = l->next)
			fprintf(fp, "%s%c", l->contact_name, l->next ? ',' : '\n');
	}
}

void fcache_contact(FILE *fp, const contact *temp_contact)
{
	commandsmember *list;
	int x;

	fprintf(fp, "define contact {\n");
	fprintf(fp, "\tcontact_name\t%s\n", temp_contact->name);
	if (temp_contact->alias)
		fprintf(fp, "\talias\t%s\n", temp_contact->alias);
	if (temp_contact->service_notification_period)
		fprintf(fp, "\tservice_notification_period\t%s\n", temp_contact->service_notification_period);
	if (temp_contact->host_notification_period)
		fprintf(fp, "\thost_notification_period\t%s\n", temp_contact->host_notification_period);
	fprintf(fp, "\tservice_notification_options\t%s\n", opts2str(temp_contact->service_notification_options, service_flag_map, 'r'));
	fprintf(fp, "\thost_notification_options\t%s\n", opts2str(temp_contact->host_notification_options, host_flag_map, 'r'));
	if (temp_contact->service_notification_commands) {
		fprintf(fp, "\tservice_notification_commands\t");
		for (list = temp_contact->service_notification_commands; list; list = list->next) {
			fprintf(fp, "%s%c", list->command, list->next ? ',' : '\n');
		}
	}
	if (temp_contact->host_notification_commands) {
		fprintf(fp, "\thost_notification_commands\t");
		for (list = temp_contact->host_notification_commands; list; list = list->next) {
			fprintf(fp, "%s%c", list->command, list->next ? ',' : '\n');
		}
	}
	if (temp_contact->email)
		fprintf(fp, "\temail\t%s\n", temp_contact->email);
	if (temp_contact->pager)
		fprintf(fp, "\tpager\t%s\n", temp_contact->pager);
	for (x = 0; x < MAX_CONTACT_ADDRESSES; x++) {
		if (temp_contact->address[x])
			fprintf(fp, "\taddress%d\t%s\n", x + 1, temp_contact->address[x]);
	}
	fprintf(fp, "\tminimum_value\t%u\n", temp_contact->minimum_value);
	fprintf(fp, "\thost_notifications_enabled\t%d\n", temp_contact->host_notifications_enabled);
	fprintf(fp, "\tservice_notifications_enabled\t%d\n", temp_contact->service_notifications_enabled);
	fprintf(fp, "\tcan_submit_commands\t%d\n", temp_contact->can_submit_commands);
	fprintf(fp, "\tretain_status_information\t%d\n", temp_contact->retain_status_information);
	fprintf(fp, "\tretain_nonstatus_information\t%d\n", temp_contact->retain_nonstatus_information);

	/* custom variables */
	fcache_customvars(fp, temp_contact->custom_variables);
	fprintf(fp, "\t}\n\n");
}
