#include "objects_hostgroup.h"
#include "objects_host.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"
#include "globals.h"
#include <string.h>

static dkhash_table *hostgroup_hash_table = NULL;
hostgroup *hostgroup_list = NULL;
hostgroup **hostgroup_ary = NULL;

int init_objects_hostgroup(int elems)
{
	if (!elems) {
		hostgroup_ary = NULL;
		hostgroup_hash_table = NULL;
		return ERROR;
	}
	hostgroup_ary = nm_calloc(elems, sizeof(hostgroup*));
	hostgroup_hash_table = dkhash_create(elems * 1.5);
	return OK;
}

void destroy_objects_hostgroup()
{
	unsigned int i;
	for (i = 0; i < num_objects.hostgroups; i++) {
		hostgroup *this_hostgroup = hostgroup_ary[i];
		destroy_hostgroup(this_hostgroup);
	}
	hostgroup_list = NULL;
	dkhash_destroy(hostgroup_hash_table);
	hostgroup_hash_table = NULL;
	nm_free(hostgroup_ary);
	num_objects.hostgroups = 0;
}

hostgroup *create_hostgroup(char *name, char *alias, char *notes, char *notes_url, char *action_url)
{
	hostgroup *new_hostgroup = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup name is NULL\n");
		return NULL;
	}

	new_hostgroup = nm_calloc(1, sizeof(*new_hostgroup));

	/* assign vars */
	new_hostgroup->group_name = name;
	new_hostgroup->alias = alias ? alias : name;
	new_hostgroup->notes = notes;
	new_hostgroup->notes_url = notes_url;
	new_hostgroup->action_url = action_url;

	/* handle errors */
	if (result == ERROR) {
		free(new_hostgroup);
		return NULL;
	}
	return new_hostgroup;
}

int register_hostgroup(hostgroup *new_hostgroup)
{
	int result = dkhash_insert(hostgroup_hash_table, new_hostgroup->group_name, NULL, new_hostgroup);
	switch (result) {
	case DKHASH_EDUPE:
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup '%s' has already been defined\n", new_hostgroup->group_name);
		return ERROR;
		break;
	case DKHASH_OK:
		break;
	default:
		nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add hostgroup '%s' to hash table\n", new_hostgroup->group_name);
		return ERROR;
		break;
	}
	new_hostgroup->id = num_objects.hostgroups++;
	hostgroup_ary[new_hostgroup->id] = new_hostgroup;
	if (new_hostgroup->id)
		hostgroup_ary[new_hostgroup->id - 1]->next = new_hostgroup;
	else
		hostgroup_list = new_hostgroup;

	return OK;
}

void destroy_hostgroup(hostgroup *this_hostgroup)
{
	/* free memory for the group members */
	hostsmember *this_hostsmember = this_hostgroup->members;
	while (this_hostsmember != NULL) {
		hostsmember *next_hostsmember = this_hostsmember->next;
		nm_free(this_hostsmember);
		this_hostsmember = next_hostsmember;
	}

	if (this_hostgroup->alias != this_hostgroup->group_name)
		nm_free(this_hostgroup->alias);
	nm_free(this_hostgroup->group_name);
	nm_free(this_hostgroup->notes);
	nm_free(this_hostgroup->notes_url);
	nm_free(this_hostgroup->action_url);
	nm_free(this_hostgroup);
}

/* add a new host to a host group */
hostsmember *add_host_to_hostgroup(hostgroup *temp_hostgroup, char *host_name)
{
	hostsmember *new_member = NULL;
	hostsmember *last_member = NULL;
	hostsmember *temp_member = NULL;
	struct host *h;

	/* make sure we have the data we need */
	if (temp_hostgroup == NULL || (host_name == NULL || !strcmp(host_name, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup or group member is NULL\n");
		return NULL;
	}
	if (!(h = find_host(host_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate host '%s' for hostgroup '%s'\n", host_name, temp_hostgroup->group_name);
		return NULL;
	}

	/* allocate memory for a new member */
	new_member = nm_calloc(1, sizeof(hostsmember));
	/* assign vars */
	new_member->host_name = h->name;
	new_member->host_ptr = h;

	/* add (unsorted) link from the host to its group */
	prepend_object_to_objectlist(&h->hostgroups_ptr, (void *)temp_hostgroup);

	/* add the new member to the member list, sorted by host name */
	if (use_large_installation_tweaks == TRUE) {
		new_member->next = temp_hostgroup->members;
		temp_hostgroup->members = new_member;
		return new_member;
	}
	last_member = temp_hostgroup->members;
	for (temp_member = temp_hostgroup->members; temp_member != NULL; temp_member = temp_member->next) {
		if (strcmp(new_member->host_name, temp_member->host_name) < 0) {
			new_member->next = temp_member;
			if (temp_member == temp_hostgroup->members)
				temp_hostgroup->members = new_member;
			else
				last_member->next = new_member;
			break;
		} else
			last_member = temp_member;
	}
	if (temp_hostgroup->members == NULL) {
		new_member->next = NULL;
		temp_hostgroup->members = new_member;
	} else if (temp_member == NULL) {
		new_member->next = NULL;
		last_member->next = new_member;
	}

	return new_member;
}

hostgroup *find_hostgroup(const char *name)
{
	return dkhash_get(hostgroup_hash_table, name, NULL);
}

/*  tests whether a host is a member of a particular hostgroup */
/* NOTE: This function is only used by external modules */
int is_host_member_of_hostgroup(hostgroup *group, host *hst)
{
	hostsmember *temp_hostsmember = NULL;

	if (group == NULL || hst == NULL)
		return FALSE;

	for (temp_hostsmember = group->members; temp_hostsmember != NULL; temp_hostsmember = temp_hostsmember->next) {
		if (temp_hostsmember->host_ptr == hst)
			return TRUE;
	}

	return FALSE;
}

void fcache_hostgroup(FILE *fp, hostgroup *temp_hostgroup)
{
	fprintf(fp, "define hostgroup {\n");
	fprintf(fp, "\thostgroup_name\t%s\n", temp_hostgroup->group_name);
	if (temp_hostgroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_hostgroup->alias);
	if (temp_hostgroup->members) {
		hostsmember *list;
		fprintf(fp, "\tmembers\t");
		for (list = temp_hostgroup->members; list; list = list->next)
			fprintf(fp, "%s%c", list->host_name, list->next ? ',' : '\n');
	}
	if (temp_hostgroup->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_hostgroup->notes);
	if (temp_hostgroup->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_hostgroup->notes_url);
	if (temp_hostgroup->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_hostgroup->action_url);
	fprintf(fp, "\t}\n\n");
}
