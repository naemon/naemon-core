#include "objects_servicegroup.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"
#include "globals.h"
#include <string.h>
#include <glib.h>

static GHashTable *servicegroup_hash_table = NULL;
servicegroup *servicegroup_list = NULL;
servicegroup **servicegroup_ary = NULL;

int init_objects_servicegroup(int elems)
{
	if (!elems) {
		servicegroup_ary = NULL;
		servicegroup_hash_table = NULL;
		return ERROR;
	}
	servicegroup_ary = nm_calloc(elems, sizeof(servicegroup*));
	servicegroup_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	return OK;
}

void destroy_objects_servicegroup()
{
	unsigned int i;
	for (i = 0; i < num_objects.servicegroups; i++) {
		servicegroup *this_servicegroup = servicegroup_ary[i];
		destroy_servicegroup(this_servicegroup);
	}
	servicegroup_list = NULL;
	g_hash_table_destroy(servicegroup_hash_table);
	servicegroup_hash_table = NULL;
	nm_free(servicegroup_ary);
	num_objects.servicegroups = 0;
}

servicegroup *create_servicegroup(const char *name, const char *alias, const char *notes, const char *notes_url, const char *action_url)
{
	servicegroup *new_servicegroup = NULL;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup name is NULL\n");
		return NULL;
	}

	if (find_servicegroup(name)) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup '%s' has already been defined\n", name);
	}

	new_servicegroup = nm_calloc(1, sizeof(*new_servicegroup));

	/* duplicate vars */
	new_servicegroup->group_name = nm_strdup(name);
	new_servicegroup->alias = alias ? nm_strdup(alias) : new_servicegroup->group_name;
	new_servicegroup->notes = notes ? nm_strdup(notes) : NULL;
	new_servicegroup->notes_url = notes_url ? nm_strdup(notes_url) : NULL;
	new_servicegroup->action_url = action_url ? nm_strdup(action_url) : NULL;

	return new_servicegroup;
}

int register_servicegroup(servicegroup *new_servicegroup)
{
	if ((find_servicegroup(new_servicegroup->group_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup '%s' has already been defined\n", new_servicegroup->group_name);
		return ERROR;
	}

	g_hash_table_insert(servicegroup_hash_table, new_servicegroup->group_name, new_servicegroup);

	new_servicegroup->id = num_objects.servicegroups++;
	servicegroup_ary[new_servicegroup->id] = new_servicegroup;
	if (new_servicegroup->id)
		servicegroup_ary[new_servicegroup->id - 1]->next = new_servicegroup;
	else
		servicegroup_list = new_servicegroup;
	return OK;
}

void destroy_servicegroup(servicegroup *this_servicegroup)
{
	/* free memory for the group members */
	while (this_servicegroup->members != NULL) {
		remove_service_from_servicegroup(this_servicegroup, this_servicegroup->members->service_ptr);
	}

	if (this_servicegroup->alias != this_servicegroup->group_name)
		nm_free(this_servicegroup->alias);
	nm_free(this_servicegroup->group_name);
	nm_free(this_servicegroup->notes);
	nm_free(this_servicegroup->notes_url);
	nm_free(this_servicegroup->action_url);
	nm_free(this_servicegroup);
}

/* add a new service to a service group */
servicesmember *add_service_to_servicegroup(servicegroup *temp_servicegroup, service *svc)
{
	servicesmember *new_member = NULL;

	/* make sure we have the data we need */
	if (temp_servicegroup == NULL || svc == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup or group member is NULL\n");
		return NULL;
	}

	/* allocate memory for a new member */
	new_member = nm_calloc(1, sizeof(servicesmember));

	/* assign vars */
	new_member->host_name = svc->host_name;
	new_member->service_description = svc->description;
	new_member->service_ptr = svc;

	/* add (unsorted) link from the service to its groups */
	prepend_object_to_objectlist(&svc->servicegroups_ptr, temp_servicegroup);

	/*
	 * add new member to member list, sorted by host name then
	 * service description, unless we're a large installation, in
	 * which case insertion-sorting will take far too long
	 */
	if (use_large_installation_tweaks == TRUE) {
		new_member->next = temp_servicegroup->members;
		temp_servicegroup->members = new_member;
		return new_member;
	}
	last_member = temp_servicegroup->members;
	for (temp_member = temp_servicegroup->members; temp_member != NULL; temp_member = temp_member->next) {

		if (strcmp(new_member->host_name, temp_member->host_name) < 0) {
			new_member->next = temp_member;
			if (temp_member == temp_servicegroup->members)
				temp_servicegroup->members = new_member;
			else
				last_member->next = new_member;
			break;
		}

		else if (strcmp(new_member->host_name, temp_member->host_name) == 0 && strcmp(new_member->service_description, temp_member->service_description) < 0) {
			new_member->next = temp_member;
			if (temp_member == temp_servicegroup->members)
				temp_servicegroup->members = new_member;
			else
				last_member->next = new_member;
			break;
		}

		else
			last_member = temp_member;
	}
	if (temp_servicegroup->members == NULL) {
		new_member->next = NULL;
		temp_servicegroup->members = new_member;
	} else if (temp_member == NULL) {
		new_member->next = NULL;
		last_member->next = new_member;
	}

	return new_member;
}

void remove_service_from_servicegroup(servicegroup *temp_servicegroup, service *svc)
{
	servicesmember *this_servicesmember, *next_servicesmember, *prev_servicesmember;
	objectlist *item, *next, *prev;
	for (prev = NULL, item = svc->servicegroups_ptr;
		 item;
		 prev = item, item = next)
	{
		next = item->next;
		if (item->object_ptr == temp_servicegroup) {
			if (prev)
				prev->next = next;
			else
				svc->servicegroups_ptr = next;
			nm_free(item);
			item = prev;
		}
	}
	for (prev_servicesmember = NULL, this_servicesmember = temp_servicegroup->members;
		 this_servicesmember;
		 prev_servicesmember = this_servicesmember, this_servicesmember = next_servicesmember)
	{
		next_servicesmember = this_servicesmember->next;
		if (this_servicesmember->service_ptr == svc) {
			if (prev_servicesmember)
				prev_servicesmember->next = next_servicesmember;
			else
				temp_servicegroup->members = next_servicesmember;
			nm_free(this_servicesmember);
			this_servicesmember = prev_servicesmember;
		}
	}
}

servicegroup *find_servicegroup(const char *name)
{
	return g_hash_table_lookup(servicegroup_hash_table, name);
}

/*  tests whether a host is a member of a particular servicegroup */
/* NOTE: This function is only used by external modules (mod_gearman, f.e) */
int is_host_member_of_servicegroup(servicegroup *group, host *hst)
{
	servicesmember *temp_servicesmember = NULL;

	if (group == NULL || hst == NULL)
		return FALSE;

	for (temp_servicesmember = group->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
		if (temp_servicesmember->service_ptr != NULL && temp_servicesmember->service_ptr->host_ptr == hst)
			return TRUE;
	}

	return FALSE;
}

/*  tests whether a service is a member of a particular servicegroup */
/* NOTE: This function is only used by external modules (mod_gearman, f.e) */
int is_service_member_of_servicegroup(servicegroup *group, service *svc)
{
	servicesmember *temp_servicesmember = NULL;

	if (group == NULL || svc == NULL)
		return FALSE;

	for (temp_servicesmember = group->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
		if (temp_servicesmember->service_ptr == svc)
			return TRUE;
	}

	return FALSE;
}

void fcache_servicegroup(FILE *fp, servicegroup *temp_servicegroup)
{
	fprintf(fp, "define servicegroup {\n");
	fprintf(fp, "\tservicegroup_name\t%s\n", temp_servicegroup->group_name);
	if (temp_servicegroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_servicegroup->alias);
	if (temp_servicegroup->members) {
		servicesmember *list;
		fprintf(fp, "\tmembers\t");
		for (list = temp_servicegroup->members; list; list = list->next) {
			service *s = list->service_ptr;
			fprintf(fp, "%s,%s%c", s->host_name, s->description, list->next ? ',' : '\n');
		}
	}
	if (temp_servicegroup->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_servicegroup->notes);
	if (temp_servicegroup->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_servicegroup->notes_url);
	if (temp_servicegroup->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_servicegroup->action_url);
	fprintf(fp, "\t}\n\n");
}
