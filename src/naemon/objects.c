#include <string.h>
#include "config.h"
#include "common.h"
#include "objects.h"
#include "objects_common.h"
#include "objects_contact.h"
#include "xodtemplate.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"


/*
 * These get created in xdata/xodtemplate.c:xodtemplate_register_objects()
 * Escalations are attached to the objects they belong to.
 * Dependencies are attached to the dependent end of the object chain.
 */
dkhash_table *object_hash_tables[NUM_HASHED_OBJECT_TYPES];

servicegroup *servicegroup_list = NULL;
hostescalation *hostescalation_list = NULL;
serviceescalation *serviceescalation_list = NULL;
servicegroup **servicegroup_ary = NULL;
hostescalation **hostescalation_ary = NULL;
serviceescalation **serviceescalation_ary = NULL;
hostdependency **hostdependency_ary = NULL;
servicedependency **servicedependency_ary = NULL;

int __nagios_object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;

static int cmp_sdep(const void *a_, const void *b_)
{
	const servicedependency *a = *(servicedependency **)a_;
	const servicedependency *b = *(servicedependency **)b_;
	int ret;
	ret = a->master_service_ptr->id - b->master_service_ptr->id;
	return ret ? ret : (int)(a->dependent_service_ptr->id - b->dependent_service_ptr->id);
}

static int cmp_hdep(const void *a_, const void *b_)
{
	const hostdependency *a = *(const hostdependency **)a_;
	const hostdependency *b = *(const hostdependency **)b_;
	int ret;
	ret = a->master_host_ptr->id - b->master_host_ptr->id;
	return ret ? ret : (int)(a->dependent_host_ptr->id - b->dependent_host_ptr->id);
}

static int cmp_serviceesc(const void *a_, const void *b_)
{
	const serviceescalation *a = *(const serviceescalation **)a_;
	const serviceescalation *b = *(const serviceescalation **)b_;
	return a->service_ptr->id - b->service_ptr->id;
}

static int cmp_hostesc(const void *a_, const void *b_)
{
	const hostescalation *a = *(const hostescalation **)a_;
	const hostescalation *b = *(const hostescalation **)b_;
	return a->host_ptr->id - b->host_ptr->id;
}


static void post_process_object_config(void)
{
	objectlist *list;
	unsigned int i, slot;

	if (hostdependency_ary)
		free(hostdependency_ary);
	if (servicedependency_ary)
		free(servicedependency_ary);

	hostdependency_ary = nm_calloc(num_objects.hostdependencies, sizeof(void *));
	servicedependency_ary = nm_calloc(num_objects.servicedependencies, sizeof(void *));

	slot = 0;
	for (i = 0; slot < num_objects.servicedependencies && i < num_objects.services; i++) {
		service *s = service_ary[i];
		for (list = s->notify_deps; list; list = list->next)
			servicedependency_ary[slot++] = (servicedependency *)list->object_ptr;
		for (list = s->exec_deps; list; list = list->next)
			servicedependency_ary[slot++] = (servicedependency *)list->object_ptr;
	}
	timing_point("Done post-processing servicedependencies\n");

	slot = 0;
	for (i = 0; slot < num_objects.hostdependencies && i < num_objects.hosts; i++) {
		host *h = host_ary[i];
		for (list = h->notify_deps; list; list = list->next)
			hostdependency_ary[slot++] = (hostdependency *)list->object_ptr;
		for (list = h->exec_deps; list; list = list->next)
			hostdependency_ary[slot++] = (hostdependency *)list->object_ptr;
	}
	timing_point("Done post-processing host dependencies\n");

	if (servicedependency_ary)
		qsort(servicedependency_ary, num_objects.servicedependencies, sizeof(servicedependency *), cmp_sdep);
	if (hostdependency_ary)
		qsort(hostdependency_ary, num_objects.hostdependencies, sizeof(hostdependency *), cmp_hdep);
	if (hostescalation_ary)
		qsort(hostescalation_ary, num_objects.hostescalations, sizeof(hostescalation *), cmp_hostesc);
	if (serviceescalation_ary)
		qsort(serviceescalation_ary, num_objects.serviceescalations, sizeof(serviceescalation *), cmp_serviceesc);
	timing_point("Done post-sorting slave objects\n");

	servicegroup_list = servicegroup_ary ? *servicegroup_ary : NULL;
	hostescalation_list = hostescalation_ary ? *hostescalation_ary : NULL;
	serviceescalation_list = serviceescalation_ary ? *serviceescalation_ary : NULL;
}


/******************************************************************/
/******* TOP-LEVEL HOST CONFIGURATION DATA INPUT FUNCTION *********/
/******************************************************************/

/* read all host configuration data from external source */
int read_object_config_data(const char *main_config_file, int options)
{
	int result = OK;

	/* reset object counts */
	memset(&num_objects, 0, sizeof(num_objects));

	/* read in data from all text host config files (template-based) */
	result = xodtemplate_read_config_data(main_config_file, options);
	if (result != OK)
		return ERROR;

	/* handle any remaining config mangling */
	post_process_object_config();
	timing_point("Done post-processing configuration\n");

	return result;
}

/******************************************************************/
/**************** OBJECT ADDITION FUNCTIONS ***********************/
/******************************************************************/

static int create_object_table(const char *name, unsigned int elems, unsigned int size, void **ptr)
{
	if (!elems) {
		*ptr = NULL;
		return OK;
	}
	*ptr = nm_calloc(elems, size);
	return OK;
}

#define mktable(name, id) \
	create_object_table(#name, ocount[id], sizeof(name *), (void **)&name##_ary)

/* ocount is an array with NUM_OBJECT_TYPES members */
int create_object_tables(unsigned int *ocount)
{
	int i;

	for (i = 0; i < NUM_HASHED_OBJECT_TYPES; i++) {
		const unsigned int hash_size = ocount[i] * 1.5;
		if (!hash_size)
			continue;
		object_hash_tables[i] = dkhash_create(hash_size);
		if (!object_hash_tables[i]) {
			nm_log(NSLOG_CONFIG_ERROR, "Failed to create hash table with %u entries\n", hash_size);
		}
	}

	/*
	 * errors here will always lead to an early exit, so there's no need
	 * to free() successful allocs when later ones fail
	 */
	if (mktable(servicegroup, OBJTYPE_SERVICEGROUP) != OK)
		return ERROR;
	if (mktable(hostescalation, OBJTYPE_HOSTESCALATION) != OK)
		return ERROR;
	if (mktable(hostdependency, OBJTYPE_HOSTDEPENDENCY) != OK)
		return ERROR;
	if (mktable(serviceescalation, OBJTYPE_SERVICEESCALATION) != OK)
		return ERROR;
	if (mktable(servicedependency, OBJTYPE_SERVICEDEPENDENCY) != OK)
		return ERROR;

	return OK;
}


/* add a new service group to the list in memory */
servicegroup *add_servicegroup(char *name, char *alias, char *notes, char *notes_url, char *action_url)
{
	servicegroup *new_servicegroup = NULL;
	int result = OK;

	/* make sure we have the data we need */
	if (name == NULL || !strcmp(name, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup name is NULL\n");
		return NULL;
	}

	new_servicegroup = nm_calloc(1, sizeof(*new_servicegroup));

	/* duplicate vars */
	new_servicegroup->group_name = name;
	new_servicegroup->alias = alias ? alias : name;
	new_servicegroup->notes = notes;
	new_servicegroup->notes_url = notes_url;
	new_servicegroup->action_url = action_url;

	/* add new service group to hash table */
	if (result == OK) {
		result = dkhash_insert(object_hash_tables[OBJTYPE_SERVICEGROUP], new_servicegroup->group_name, NULL, new_servicegroup);
		switch (result) {
		case DKHASH_EDUPE:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup '%s' has already been defined\n", name);
			result = ERROR;
			break;
		case DKHASH_OK:
			result = OK;
			break;
		default:
			nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add servicegroup '%s' to hash table\n", name);
			result = ERROR;
			break;
		}
	}

	/* handle errors */
	if (result == ERROR) {
		nm_free(new_servicegroup);
		return NULL;
	}

	new_servicegroup->id = num_objects.servicegroups++;
	servicegroup_ary[new_servicegroup->id] = new_servicegroup;
	if (new_servicegroup->id)
		servicegroup_ary[new_servicegroup->id - 1]->next = new_servicegroup;
	return new_servicegroup;
}


/* add a new service to a service group */
servicesmember *add_service_to_servicegroup(servicegroup *temp_servicegroup, char *host_name, char *svc_description)
{
	servicesmember *new_member = NULL;
	servicesmember *last_member = NULL;
	servicesmember *temp_member = NULL;
	struct service *svc;

	/* make sure we have the data we need */
	if (temp_servicegroup == NULL || (host_name == NULL || !strcmp(host_name, "")) || (svc_description == NULL || !strcmp(svc_description, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Servicegroup or group member is NULL\n");
		return NULL;
	}
	if (!(svc = find_service(host_name, svc_description))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate service '%s' on host '%s' for servicegroup '%s'\n", svc_description, host_name, temp_servicegroup->group_name);
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


/* add a new service escalation to the list in memory */
serviceescalation *add_serviceescalation(char *host_name, char *description, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options)
{
	serviceescalation *new_serviceescalation = NULL;
	service *svc;
	timeperiod *tp = NULL;

	/* make sure we have the data we need */
	if (host_name == NULL || !*host_name || description == NULL || !*description) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Service escalation host name or description is NULL\n");
		return NULL;
	}
	if (!(svc = find_service(host_name, description))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Service '%s' on host '%s' has an escalation but is not defined anywhere!\n",
		       host_name, description);
		return NULL;
	}
	if (escalation_period && !(tp = find_timeperiod(escalation_period))) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Escalation period '%s' specified in service escalation for service '%s' on host '%s' is not defined anywhere!\n",
		       escalation_period, description, host_name);
		return NULL ;
	}

	new_serviceescalation = nm_calloc(1, sizeof(*new_serviceescalation));

	if (prepend_object_to_objectlist(&svc->escalation_list, new_serviceescalation) != OK) {
		nm_log(NSLOG_CONFIG_ERROR, "Could not add escalation to service '%s' on host '%s'\n",
		       svc->host_name, svc->description);
		return NULL;
	}

	/* assign vars. object names are immutable, so no need to copy */
	new_serviceescalation->host_name = svc->host_name;
	new_serviceescalation->description = svc->description;
	new_serviceescalation->service_ptr = svc;
	new_serviceescalation->escalation_period_ptr = tp;
	if (tp)
		new_serviceescalation->escalation_period = tp->name;

	new_serviceescalation->first_notification = first_notification;
	new_serviceescalation->last_notification = last_notification;
	new_serviceescalation->notification_interval = (notification_interval <= 0) ? 0 : notification_interval;
	new_serviceescalation->escalation_options = escalation_options;

	new_serviceescalation->id = num_objects.serviceescalations++;
	serviceescalation_ary[new_serviceescalation->id] = new_serviceescalation;
	return new_serviceescalation;
}


/* adds a contact group to a service escalation */
contactgroupsmember *add_contactgroup_to_serviceescalation(serviceescalation *se, char *group_name)
{
	return add_contactgroup_to_object(&se->contact_groups, group_name);
}


/* adds a contact to a service escalation */
contactsmember *add_contact_to_serviceescalation(serviceescalation *se, char *contact_name)
{
	return add_contact_to_object(&se->contacts, contact_name);
}

/* adds a service dependency definition */
servicedependency *add_service_dependency(char *dependent_host_name, char *dependent_service_description, char *host_name, char *service_description, int dependency_type, int inherits_parent, int failure_options, char *dependency_period)
{
	servicedependency *new_servicedependency = NULL;
	service *parent, *child;
	timeperiod *tp = NULL;
	int result;
	size_t sdep_size = sizeof(*new_servicedependency);

	/* make sure we have what we need */
	parent = find_service(host_name, service_description);
	if (!parent) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Master service '%s' on host '%s' is not defined anywhere!\n",
		       service_description, host_name);
		return NULL;
	}
	child = find_service(dependent_host_name, dependent_service_description);
	if (!child) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Dependent service '%s' on host '%s' is not defined anywhere!\n",
		       dependent_service_description, dependent_host_name);
		return NULL;
	}
	if (dependency_period && !(tp = find_timeperiod(dependency_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate timeperiod '%s' for dependency from service '%s' on host '%s' to service '%s' on host '%s'\n",
		       dependency_period, dependent_service_description, dependent_host_name, service_description, host_name);
		return NULL;
	}

	/* allocate memory for a new service dependency entry */
	new_servicedependency = nm_calloc(1, sizeof(*new_servicedependency));

	new_servicedependency->dependent_service_ptr = child;
	new_servicedependency->master_service_ptr = parent;
	new_servicedependency->dependency_period_ptr = tp;

	/* assign vars. object names are immutable, so no need to copy */
	new_servicedependency->dependent_host_name = child->host_name;
	new_servicedependency->dependent_service_description = child->description;
	new_servicedependency->host_name = parent->host_name;
	new_servicedependency->service_description = parent->description;
	if (tp)
		new_servicedependency->dependency_period = tp->name;

	new_servicedependency->dependency_type = (dependency_type == EXECUTION_DEPENDENCY) ? EXECUTION_DEPENDENCY : NOTIFICATION_DEPENDENCY;
	new_servicedependency->inherits_parent = (inherits_parent > 0) ? TRUE : FALSE;
	new_servicedependency->failure_options = failure_options;

	/*
	 * add new service dependency to its respective services.
	 * Ordering doesn't matter here as we'll have to check them
	 * all anyway. We avoid adding dupes though, since we can
	 * apparently get zillion's and zillion's of them.
	 */
	if (dependency_type == NOTIFICATION_DEPENDENCY)
		result = prepend_unique_object_to_objectlist_ptr(&child->notify_deps, new_servicedependency, &compare_objects, &sdep_size);
	else
		result = prepend_unique_object_to_objectlist_ptr(&child->exec_deps, new_servicedependency, &compare_objects, &sdep_size);

	if (result != OK) {
		free(new_servicedependency);
		/* hack to avoid caller bombing out */
		return result == OBJECTLIST_DUPE ? (void *)1 : NULL;
	}

	num_objects.servicedependencies++;
	return new_servicedependency;
}


/* adds a host dependency definition */
hostdependency *add_host_dependency(char *dependent_host_name, char *host_name, int dependency_type, int inherits_parent, int failure_options, char *dependency_period)
{
	hostdependency *new_hostdependency = NULL;
	host *parent, *child;
	timeperiod *tp = NULL;
	int result;
	size_t hdep_size = sizeof(*new_hostdependency);

	/* make sure we have what we need */
	parent = find_host(host_name);
	if (!parent) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Master host '%s' in hostdependency from '%s' to '%s' is not defined anywhere!\n",
		       host_name, dependent_host_name, host_name);
		return NULL;
	}
	child = find_host(dependent_host_name);
	if (!child) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Dependent host '%s' in hostdependency from '%s' to '%s' is not defined anywhere!\n",
		       dependent_host_name, dependent_host_name, host_name);
		return NULL;
	}
	if (dependency_period && !(tp = find_timeperiod(dependency_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Unable to locate dependency_period '%s' for %s->%s host dependency\n",
		       dependency_period, parent->name, child->name);
		return NULL ;
	}

	new_hostdependency = nm_calloc(1, sizeof(*new_hostdependency));
	new_hostdependency->dependent_host_ptr = child;
	new_hostdependency->master_host_ptr = parent;
	new_hostdependency->dependency_period_ptr = tp;

	/* assign vars. Objects are immutable, so no need to copy */
	new_hostdependency->dependent_host_name = child->name;
	new_hostdependency->host_name = parent->name;
	if (tp)
		new_hostdependency->dependency_period = tp->name;

	new_hostdependency->dependency_type = (dependency_type == EXECUTION_DEPENDENCY) ? EXECUTION_DEPENDENCY : NOTIFICATION_DEPENDENCY;
	new_hostdependency->inherits_parent = (inherits_parent > 0) ? TRUE : FALSE;
	new_hostdependency->failure_options = failure_options;

	if (dependency_type == NOTIFICATION_DEPENDENCY)
		result = prepend_unique_object_to_objectlist_ptr(&child->notify_deps, new_hostdependency, *compare_objects, &hdep_size);
	else
		result = prepend_unique_object_to_objectlist_ptr(&child->exec_deps, new_hostdependency, *compare_objects, &hdep_size);

	if (result != OK) {
		free(new_hostdependency);
		/* hack to avoid caller bombing out */
		return result == OBJECTLIST_DUPE ? (void *)1 : NULL;
	}

	num_objects.hostdependencies++;
	return new_hostdependency;
}


/* add a new host escalation to the list in memory */
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
	hostescalation_ary[new_hostescalation->id] = new_hostescalation;
	return new_hostescalation;
}


/* adds a contact group to a host escalation */
contactgroupsmember *add_contactgroup_to_hostescalation(hostescalation *he, char *group_name)
{
	return add_contactgroup_to_object(&he->contact_groups, group_name);
}


/* adds a contact to a host escalation */
contactsmember *add_contact_to_hostescalation(hostescalation *he, char *contact_name)
{

	return add_contact_to_object(&he->contacts, contact_name);
}


/******************************************************************/
/******************** OBJECT SEARCH FUNCTIONS *********************/
/******************************************************************/

servicegroup *find_servicegroup(const char *name)
{
	return dkhash_get(object_hash_tables[OBJTYPE_SERVICEGROUP], name, NULL);
}

/******************************************************************/
/********************* OBJECT QUERY FUNCTIONS *********************/
/******************************************************************/

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


/******************************************************************/
/******************* OBJECT DELETION FUNCTIONS ********************/
/******************************************************************/

/* free all allocated memory for objects */
int free_object_data(void)
{
	servicesmember *this_servicesmember = NULL;
	servicesmember *next_servicesmember = NULL;
	contactsmember *this_contactsmember = NULL;
	contactsmember *next_contactsmember = NULL;
	contactgroupsmember *this_contactgroupsmember = NULL;
	contactgroupsmember *next_contactgroupsmember = NULL;
	unsigned int i = 0;


	/*
	 * kill off hash tables so lingering modules don't look stuff up
	 * while we're busy removing it.
	 */
	for (i = 0; i < ARRAY_SIZE(object_hash_tables); i++) {
		dkhash_table *t = object_hash_tables[i];
		object_hash_tables[i] = NULL;
		dkhash_destroy(t);
	}

	/**** free memory for the service group list ****/
	for (i = 0; i < num_objects.servicegroups; i++) {
		servicegroup *this_servicegroup = servicegroup_ary[i];

		/* free memory for the group members */
		this_servicesmember = this_servicegroup->members;
		while (this_servicesmember != NULL) {
			next_servicesmember = this_servicesmember->next;
			nm_free(this_servicesmember);
			this_servicesmember = next_servicesmember;
		}

		if (this_servicegroup->alias != this_servicegroup->group_name)
			nm_free(this_servicegroup->alias);
		nm_free(this_servicegroup->group_name);
		nm_free(this_servicegroup->notes);
		nm_free(this_servicegroup->notes_url);
		nm_free(this_servicegroup->action_url);
		nm_free(this_servicegroup);
	}

	/* reset pointers */
	nm_free(servicegroup_ary);

	/**** free service escalation memory ****/
	for (i = 0; i < num_objects.serviceescalations; i++) {
		serviceescalation *this_serviceescalation = serviceescalation_ary[i];

		/* free memory for the contact group members */
		this_contactgroupsmember = this_serviceescalation->contact_groups;
		while (this_contactgroupsmember != NULL) {
			next_contactgroupsmember = this_contactgroupsmember->next;
			nm_free(this_contactgroupsmember);
			this_contactgroupsmember = next_contactgroupsmember;
		}

		/* free memory for contacts */
		this_contactsmember = this_serviceescalation->contacts;
		while (this_contactsmember != NULL) {
			next_contactsmember = this_contactsmember->next;
			nm_free(this_contactsmember);
			this_contactsmember = next_contactsmember;
		}
		nm_free(this_serviceescalation);
	}

	/* reset pointers */
	nm_free(serviceescalation_ary);


	/**** free service dependency memory ****/
	if (servicedependency_ary) {
		for (i = 0; i < num_objects.servicedependencies; i++)
			nm_free(servicedependency_ary[i]);
		nm_free(servicedependency_ary);
	}


	/**** free host dependency memory ****/
	if (hostdependency_ary) {
		for (i = 0; i < num_objects.hostdependencies; i++)
			nm_free(hostdependency_ary[i]);
		nm_free(hostdependency_ary);
	}


	/**** free host escalation memory ****/
	for (i = 0; i < num_objects.hostescalations; i++) {
		hostescalation *this_hostescalation = hostescalation_ary[i];

		/* free memory for the contact group members */
		this_contactgroupsmember = this_hostescalation->contact_groups;
		while (this_contactgroupsmember != NULL) {
			next_contactgroupsmember = this_contactgroupsmember->next;
			nm_free(this_contactgroupsmember);
			this_contactgroupsmember = next_contactgroupsmember;
		}

		/* free memory for contacts */
		this_contactsmember = this_hostescalation->contacts;
		while (this_contactsmember != NULL) {
			next_contactsmember = this_contactsmember->next;
			nm_free(this_contactsmember);
			this_contactsmember = next_contactsmember;
		}
		nm_free(this_hostescalation);
	}

	/* reset pointers */
	nm_free(hostescalation_ary);

	/* we no longer have any objects */
	memset(&num_objects, 0, sizeof(num_objects));

	return OK;
}


/******************************************************************/
/*********************** CACHE FUNCTIONS **************************/
/******************************************************************/

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

void fcache_servicedependency(FILE *fp, servicedependency *temp_servicedependency)
{
	fprintf(fp, "define servicedependency {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_servicedependency->host_name);
	fprintf(fp, "\tservice_description\t%s\n", temp_servicedependency->service_description);
	fprintf(fp, "\tdependent_host_name\t%s\n", temp_servicedependency->dependent_host_name);
	fprintf(fp, "\tdependent_service_description\t%s\n", temp_servicedependency->dependent_service_description);
	if (temp_servicedependency->dependency_period)
		fprintf(fp, "\tdependency_period\t%s\n", temp_servicedependency->dependency_period);
	fprintf(fp, "\tinherits_parent\t%d\n", temp_servicedependency->inherits_parent);
	fprintf(fp, "\t%s_failure_options\t%s\n",
	        temp_servicedependency->dependency_type == NOTIFICATION_DEPENDENCY ? "notification" : "execution",
	        opts2str(temp_servicedependency->failure_options, service_flag_map, 'o'));
	fprintf(fp, "\t}\n\n");
}

void fcache_serviceescalation(FILE *fp, serviceescalation *temp_serviceescalation)
{
	fprintf(fp, "define serviceescalation {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_serviceescalation->host_name);
	fprintf(fp, "\tservice_description\t%s\n", temp_serviceescalation->description);
	fprintf(fp, "\tfirst_notification\t%d\n", temp_serviceescalation->first_notification);
	fprintf(fp, "\tlast_notification\t%d\n", temp_serviceescalation->last_notification);
	fprintf(fp, "\tnotification_interval\t%f\n", temp_serviceescalation->notification_interval);
	if (temp_serviceescalation->escalation_period)
		fprintf(fp, "\tescalation_period\t%s\n", temp_serviceescalation->escalation_period);
	fprintf(fp, "\tescalation_options\t%s\n", opts2str(temp_serviceescalation->escalation_options, service_flag_map, 'r'));

	if (temp_serviceescalation->contacts) {
		contactsmember *cl;
		fprintf(fp, "\tcontacts\t");
		for (cl = temp_serviceescalation->contacts; cl; cl = cl->next)
			fprintf(fp, "%s%c", cl->contact_ptr->name, cl->next ? ',' : '\n');
	}
	if (temp_serviceescalation->contact_groups) {
		contactgroupsmember *cgl;
		fprintf(fp, "\tcontact_groups\t");
		for (cgl = temp_serviceescalation->contact_groups; cgl; cgl = cgl->next)
			fprintf(fp, "%s%c", cgl->group_name, cgl->next ? ',' : '\n');
	}
	fprintf(fp, "\t}\n\n");
}

void fcache_hostdependency(FILE *fp, hostdependency *temp_hostdependency)
{
	fprintf(fp, "define hostdependency {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_hostdependency->host_name);
	fprintf(fp, "\tdependent_host_name\t%s\n", temp_hostdependency->dependent_host_name);
	if (temp_hostdependency->dependency_period)
		fprintf(fp, "\tdependency_period\t%s\n", temp_hostdependency->dependency_period);
	fprintf(fp, "\tinherits_parent\t%d\n", temp_hostdependency->inherits_parent);
	fprintf(fp, "\t%s_failure_options\t%s\n",
	        temp_hostdependency->dependency_type == NOTIFICATION_DEPENDENCY ? "notification" : "execution",
	        opts2str(temp_hostdependency->failure_options, host_flag_map, 'o'));
	fprintf(fp, "\t}\n\n");
}

void fcache_hostescalation(FILE *fp, hostescalation *temp_hostescalation)
{
	fprintf(fp, "define hostescalation {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_hostescalation->host_name);
	fprintf(fp, "\tfirst_notification\t%d\n", temp_hostescalation->first_notification);
	fprintf(fp, "\tlast_notification\t%d\n", temp_hostescalation->last_notification);
	fprintf(fp, "\tnotification_interval\t%f\n", temp_hostescalation->notification_interval);
	if (temp_hostescalation->escalation_period)
		fprintf(fp, "\tescalation_period\t%s\n", temp_hostescalation->escalation_period);
	fprintf(fp, "\tescalation_options\t%s\n", opts2str(temp_hostescalation->escalation_options, host_flag_map, 'r'));

	fcache_contactlist(fp, "\tcontacts\t", temp_hostescalation->contacts);
	fcache_contactgrouplist(fp, "\tcontact_groups\t", temp_hostescalation->contact_groups);
	fprintf(fp, "\t}\n\n");
}

/* writes cached object definitions for use by web interface */
int fcache_objects(char *cache_file)
{
	FILE *fp = NULL;
	time_t current_time = 0L;
	unsigned int i;

	/* some people won't want to cache their objects */
	if (!cache_file || !strcmp(cache_file, "/dev/null"))
		return OK;

	time(&current_time);

	/* open the cache file for writing */
	fp = fopen(cache_file, "w");
	if (fp == NULL) {
		nm_log(NSLOG_CONFIG_WARNING, "Warning: Could not open object cache file '%s' for writing!\n", cache_file);
		return ERROR;
	}

	/* write header to cache file */
	fprintf(fp, "########################################\n");
	fprintf(fp, "#       NAGIOS OBJECT CACHE FILE\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# THIS FILE IS AUTOMATICALLY GENERATED\n");
	fprintf(fp, "# BY NAGIOS.  DO NOT MODIFY THIS FILE!\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# Created: %s", ctime(&current_time));
	fprintf(fp, "########################################\n\n");


	/* cache timeperiods */
	for (i = 0; i < num_objects.timeperiods; i++)
		fcache_timeperiod(fp, timeperiod_ary[i]);

	/* cache commands */
	for (i = 0; i < num_objects.commands; i++)
		fcache_command(fp, command_ary[i]);

	/* cache contactgroups */
	for (i = 0; i < num_objects.contactgroups; i++)
		fcache_contactgroup(fp, contactgroup_ary[i]);

	/* cache hostgroups */
	for (i = 0; i < num_objects.hostgroups; i++)
		fcache_hostgroup(fp, hostgroup_ary[i]);

	/* cache servicegroups */
	for (i = 0; i < num_objects.servicegroups; i++)
		fcache_servicegroup(fp, servicegroup_ary[i]);

	/* cache contacts */
	for (i = 0; i < num_objects.contacts; i++)
		fcache_contact(fp, contact_ary[i]);

	/* cache hosts */
	for (i = 0; i < num_objects.hosts; i++)
		fcache_host(fp, host_ary[i]);

	/* cache services */
	for (i = 0; i < num_objects.services; i++)
		fcache_service(fp, service_ary[i]);

	/* cache service dependencies */
	for (i = 0; i < num_objects.servicedependencies; i++)
		fcache_servicedependency(fp, servicedependency_ary[i]);

	/* cache service escalations */
	for (i = 0; i < num_objects.serviceescalations; i++)
		fcache_serviceescalation(fp, serviceescalation_ary[i]);

	/* cache host dependencies */
	for (i = 0; i < num_objects.hostdependencies; i++)
		fcache_hostdependency(fp, hostdependency_ary[i]);

	/* cache host escalations */
	for (i = 0; i < num_objects.hostescalations; i++)
		fcache_hostescalation(fp, hostescalation_ary[i]);

	fclose(fp);

	return OK;
}
