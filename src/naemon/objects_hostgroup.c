#include "objects_hostgroup.h"
#include "objects_host.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"
#include "globals.h"
#include "utils.h"
#include <string.h>
#include <glib.h>

static GHashTable *hostgroup_hash_table = NULL;
hostgroup *hostgroup_list = NULL;
hostgroup **hostgroup_ary = NULL;

int init_objects_hostgroup(int elems)
{
	hostgroup_ary = nm_calloc(elems, sizeof(hostgroup*));
	hostgroup_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
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
	if(hostgroup_hash_table)
		g_hash_table_destroy(hostgroup_hash_table);

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

	if (contains_illegal_object_chars(name) == TRUE) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: The name of hostgroup '%s' contains one or more illegal characters.", name);
		return NULL;
	}

	new_hostgroup = nm_calloc(1, sizeof(*new_hostgroup));

	/* assign vars */
	new_hostgroup->group_name = nm_strdup(name);
	new_hostgroup->alias = alias ? nm_strdup(alias) : new_hostgroup->group_name;
	new_hostgroup->notes = notes ? nm_strdup(notes) : NULL;
	new_hostgroup->notes_url = notes_url ? nm_strdup(notes_url) : NULL;
	new_hostgroup->action_url = action_url ? nm_strdup(action_url) : NULL;
	new_hostgroup->members = g_tree_new_full((GCompareDataFunc)g_strcmp0, NULL, g_free, NULL);

	return new_hostgroup;
}

int register_hostgroup(hostgroup *new_hostgroup)
{

	g_return_val_if_fail(hostgroup_hash_table != NULL, ERROR);

	if ((find_host(new_hostgroup->group_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Hostgroup '%s' has already been defined\n", new_hostgroup->group_name);
		return ERROR;
	}

	g_hash_table_insert(hostgroup_hash_table, new_hostgroup->group_name, new_hostgroup);

	new_hostgroup->id = num_objects.hostgroups++;
	hostgroup_ary[new_hostgroup->id] = new_hostgroup;
	if (new_hostgroup->id)
		hostgroup_ary[new_hostgroup->id - 1]->next = new_hostgroup;
	else
		hostgroup_list = new_hostgroup;

	return OK;
}

static gboolean my_g_tree_visit_pick_one(gpointer key, gpointer value, gpointer data) {
	gpointer *outptr = (gpointer *)data;
	*outptr = value;
	return TRUE; /* Stop traversal */
}

void destroy_hostgroup(hostgroup *this_hostgroup)
{
	if (!this_hostgroup)
		return;

	if (this_hostgroup->members) {
		struct host *curhost = NULL;
		do {
			curhost = NULL;
			g_tree_foreach(this_hostgroup->members, my_g_tree_visit_pick_one, &curhost);
			if(curhost) {
				remove_host_from_hostgroup(this_hostgroup, curhost);
			}
		} while(curhost != NULL);
		g_tree_unref(this_hostgroup->members);
	}
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

	g_tree_insert(temp_hostgroup->members, g_strdup(h->name), h);

	return OK;
}

int remove_host_from_hostgroup(hostgroup *temp_hostgroup, host *h)
{
	objectlist *item, *next, *prev;
	for (prev = NULL, item = h->hostgroups_ptr;
		 item;
		 prev = item, item = next)
	{
		next = item->next;
		if (item->object_ptr == temp_hostgroup) {
			if (prev)
				prev->next = next;
			else
				h->hostgroups_ptr = next;
			nm_free(item);
			item = prev;
		}
	}
	if (temp_hostgroup->members)
		g_tree_remove(temp_hostgroup->members, h->name);
	return 0;
}

hostgroup *find_hostgroup(const char *name)
{
	return name ? g_hash_table_lookup(hostgroup_hash_table, name) : NULL;
}

/*  tests whether a host is a member of a particular hostgroup */
/* NOTE: This function is only used by external modules */
int is_host_member_of_hostgroup(hostgroup *group, host *hst)
{
	return g_tree_lookup_extended(group->members, hst->name, NULL, NULL);
}

void fcache_hostgroup(FILE *fp, const hostgroup *temp_hostgroup)
{
	fprintf(fp, "define hostgroup {\n");
	fprintf(fp, "\thostgroup_name\t%s\n", temp_hostgroup->group_name);
	if (temp_hostgroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_hostgroup->alias);
	if (g_tree_nnodes(temp_hostgroup->members)) {
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
