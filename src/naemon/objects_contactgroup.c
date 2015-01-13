#include "objects_contactgroup.h"
#include "objects_timeperiod.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"
#include <string.h>

static dkhash_table *contactgroup_hash_table = NULL;
contactgroup *contactgroup_list = NULL;
contactgroup **contactgroup_ary = NULL;

int init_objects_contactgroup(int elems)
{
	if (!elems) {
		contactgroup_ary = NULL;
		contactgroup_hash_table = NULL;
		return ERROR;
	}
	contactgroup_ary = nm_calloc(elems, sizeof(contactgroup*));
	contactgroup_hash_table = dkhash_create(elems * 1.5);
	return OK;
}

void destroy_objects_contactgroup()
{
	unsigned int i;
	for (i = 0; i < num_objects.contactgroups; i++) {
		contactgroup *this_contactgroup = contactgroup_ary[i];
		destroy_contactgroup(this_contactgroup);
	}
	contactgroup_list = NULL;
	dkhash_destroy(contactgroup_hash_table);
	nm_free(contactgroup_ary);
	num_objects.contacts = 0;
}

/* add a new contact group to the list in memory */
contactgroup *add_contactgroup(char *name, char *alias)
{
	contactgroup *new_contactgroup = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup name is NULL\n");
		return NULL;
	}

	new_contactgroup = nm_calloc(1, sizeof(*new_contactgroup));

	/* assign vars */
	new_contactgroup->group_name = name;
	new_contactgroup->alias = alias ? alias : name;

	/* add new contact group to hash table */
	if (result == OK) {
		result = dkhash_insert(contactgroup_hash_table, new_contactgroup->group_name, NULL, new_contactgroup);
		switch (result) {
		case DKHASH_EDUPE:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup '%s' has already been defined\n", name);
			result = ERROR;
			break;
		case DKHASH_OK:
			result = OK;
			break;
		default:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add contactgroup '%s' to hash table\n", name);
			result = ERROR;
			break;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		free(new_contactgroup);
		return NULL;
	}

	new_contactgroup->id = num_objects.contactgroups++;
	contactgroup_ary[new_contactgroup->id] = new_contactgroup;
	if (new_contactgroup->id)
		contactgroup_ary[new_contactgroup->id - 1]->next = new_contactgroup;
	else
		contactgroup_list = new_contactgroup;
	return new_contactgroup;
}

void destroy_contactgroup(contactgroup *this_contactgroup)
{
	contactsmember *this_contactsmember;

	/* free memory for the group members */
	this_contactsmember = this_contactgroup->members;
	while (this_contactsmember != NULL) {
		contactsmember *next_contactsmember = this_contactsmember->next;
		nm_free(this_contactsmember);
		this_contactsmember = next_contactsmember;
	}

	if (this_contactgroup->alias != this_contactgroup->group_name)
		nm_free(this_contactgroup->alias);
	nm_free(this_contactgroup->group_name);
	nm_free(this_contactgroup);
}

/* add a new member to a contact group */
contactsmember *add_contact_to_contactgroup(contactgroup *grp, char *contact_name)
{
	contactsmember *new_contactsmember = NULL;
	struct contact *c;

	/* make sure we have the data we need */
	if (grp == NULL || (contact_name == NULL || !strcmp(contact_name, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup or contact name is NULL\n");
		return NULL;
	}

	if (!(c = find_contact(contact_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate contact '%s' for contactgroup '%s'\n", contact_name, grp->group_name);
		return NULL;
	}

	/* allocate memory for a new member */
	new_contactsmember = nm_calloc(1, sizeof(contactsmember));

	/* assign vars */
	new_contactsmember->contact_name = c->name;
	new_contactsmember->contact_ptr = c;

	/* add the new member to the head of the member list */
	new_contactsmember->next = grp->members;
	grp->members = new_contactsmember;

	prepend_object_to_objectlist(&c->contactgroups_ptr, (void *)grp);

	return new_contactsmember;
}

contactgroupsmember *add_contactgroup_to_object(contactgroupsmember **cg_list, const char *group_name)
{
	contactgroupsmember *cgm;
	contactgroup *cg;

	if (!group_name || !*group_name) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contact name is NULL\n");
		return NULL;
	}
	if (!(cg = find_contactgroup(group_name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Contactgroup '%s' is not defined anywhere\n", group_name);
		return NULL;
	}
	cgm = nm_malloc(sizeof(*cgm));
	cgm->group_name = cg->group_name;
	cgm->group_ptr = cg;
	cgm->next = *cg_list;
	*cg_list = cgm;

	return cgm;
}

contactgroup *find_contactgroup(const char *name)
{
	return dkhash_get(contactgroup_hash_table, name, NULL);
}

/*
 * tests whether a contact is a member of a particular contactgroup.
 * This function is used by external modules, such as Livestatus
 */
int is_contact_member_of_contactgroup(contactgroup *group, contact *cntct)
{
	contactsmember *member;

	if (!group || !cntct)
		return FALSE;

	/* search all contacts in this contact group */
	for (member = group->members; member; member = member->next) {
		if (member->contact_ptr == cntct)
			return TRUE;
	}

	return FALSE;
}

void fcache_contactgrouplist(FILE *fp, const char *prefix, contactgroupsmember *list)
{
	if (list) {
		contactgroupsmember *l;
		fprintf(fp, "%s", prefix);
		for (l = list; l; l = l->next)
			fprintf(fp, "%s%c", l->group_name, l->next ? ',' : '\n');
	}
}

void fcache_contactgroup(FILE *fp, contactgroup *temp_contactgroup)
{
	fprintf(fp, "define contactgroup {\n");
	fprintf(fp, "\tcontactgroup_name\t%s\n", temp_contactgroup->group_name);
	if (temp_contactgroup->alias)
		fprintf(fp, "\talias\t%s\n", temp_contactgroup->alias);
	fcache_contactlist(fp, "\tmembers\t", temp_contactgroup->members);
	fprintf(fp, "\t}\n\n");
}
