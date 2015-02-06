#include "objects_hostgroup.h"
#include "objects_host.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"
#include "globals.h"
#include "utils.h"
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

hostgroup *create_hostgroup(const char *name, const char *alias, const char *notes, const char *notes_url, const char *action_url)
{
	hostgroup *new_hostgroup = NULL;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup name is NULL\n");
		return NULL;
	}

	new_hostgroup = nm_calloc(1, sizeof(*new_hostgroup));

	/* assign vars */
	new_hostgroup->group_name = nm_strdup(name);
	new_hostgroup->alias = alias ? nm_strdup(alias) : new_hostgroup->group_name;
	new_hostgroup->notes = notes ? nm_strdup(notes) : NULL;
	new_hostgroup->notes_url = notes_url ? nm_strdup(notes_url) : NULL;
	new_hostgroup->action_url = action_url ? nm_strdup(action_url) : NULL;
	new_hostgroup->members = rbtree_create(compare_host);

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
	rbtree_destroy(this_hostgroup->members, NULL);
	this_hostgroup->members = NULL;

	if (this_hostgroup->alias != this_hostgroup->group_name)
		nm_free(this_hostgroup->alias);
	nm_free(this_hostgroup->group_name);
	nm_free(this_hostgroup->notes);
	nm_free(this_hostgroup->notes_url);
	nm_free(this_hostgroup->action_url);
	nm_free(this_hostgroup);
}

int add_host_to_hostgroup(hostgroup *temp_hostgroup, host *h)
{

	/* make sure we have the data we need */
	if (temp_hostgroup == NULL || h == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup or group member is NULL\n");
		return ERROR;
	}

	/* add (unsorted) link from the host to its group */
	prepend_object_to_objectlist(&h->hostgroups_ptr, (void *)temp_hostgroup);

	rbtree_insert(temp_hostgroup->members, h);

	return OK;
}

hostgroup *find_hostgroup(const char *name)
{
	return dkhash_get(hostgroup_hash_table, name, NULL);
}

/*  tests whether a host is a member of a particular hostgroup */
/* NOTE: This function is only used by external modules */
int is_host_member_of_hostgroup(hostgroup *group, host *hst)
{
	if (rbtree_find(group->members, hst))
		return TRUE;
	return FALSE;
}

void fcache_hostgroup(FILE *fp, hostgroup *temp_hostgroup)
{
	fprintf(fp, "define hostgroup {\n");
	fprintf(fp, "\thostgroup_name\t%s\n", temp_hostgroup->group_name);
	if (temp_hostgroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_hostgroup->alias);
	if (temp_hostgroup->members) {
		char *members = implode_hosttree(temp_hostgroup->members, ",");
		fprintf(fp, "\tmembers\t%s\n", members);
		nm_free(members);
	}
	if (temp_hostgroup->notes)
		fprintf(fp, "\tnotes\t%s\n", temp_hostgroup->notes);
	if (temp_hostgroup->notes_url)
		fprintf(fp, "\tnotes_url\t%s\n", temp_hostgroup->notes_url);
	if (temp_hostgroup->action_url)
		fprintf(fp, "\taction_url\t%s\n", temp_hostgroup->action_url);
	fprintf(fp, "\t}\n\n");
}
