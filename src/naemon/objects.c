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

serviceescalation *serviceescalation_list = NULL;
serviceescalation **serviceescalation_ary = NULL;

int __nagios_object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;

static int cmp_serviceesc(const void *a_, const void *b_)
{
	const serviceescalation *a = *(const serviceescalation **)a_;
	const serviceescalation *b = *(const serviceescalation **)b_;
	return a->service_ptr->id - b->service_ptr->id;
}


static void post_process_object_config(void)
{
	if (serviceescalation_ary)
		qsort(serviceescalation_ary, num_objects.serviceescalations, sizeof(serviceescalation *), cmp_serviceesc);
	timing_point("Done post-sorting slave objects\n");

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
	if (mktable(serviceescalation, OBJTYPE_SERVICEESCALATION) != OK)
		return ERROR;

	return OK;
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


/******************************************************************/
/******************* OBJECT DELETION FUNCTIONS ********************/
/******************************************************************/

/* free all allocated memory for objects */
int free_object_data(void)
{
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


	/* we no longer have any objects */
	memset(&num_objects, 0, sizeof(num_objects));

	return OK;
}


/******************************************************************/
/*********************** CACHE FUNCTIONS **************************/
/******************************************************************/

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
	for (i = 0; i < num_objects.services; i++) {
		struct objectlist *deplist;
		for (deplist = service_ary[i]->exec_deps; deplist; deplist = deplist->next)
			fcache_servicedependency(fp, deplist->object_ptr);
		for (deplist = service_ary[i]->notify_deps; deplist; deplist = deplist->next)
			fcache_servicedependency(fp, deplist->object_ptr);
	}

	/* cache service escalations */
	for (i = 0; i < num_objects.serviceescalations; i++)
		fcache_serviceescalation(fp, serviceescalation_ary[i]);

	/* cache host dependencies */
	for (i = 0; i < num_objects.hosts; i++) {
		struct objectlist *deplist;
		for (deplist = host_ary[i]->exec_deps; deplist; deplist = deplist->next)
			fcache_hostdependency(fp, deplist->object_ptr);
		for (deplist = host_ary[i]->notify_deps; deplist; deplist = deplist->next)
			fcache_hostdependency(fp, deplist->object_ptr);
	}

	/* cache host escalations */
	for (i = 0; i < num_objects.hosts; i++) {
		struct objectlist *esclist;
		for (esclist = host_ary[i]->escalation_list; esclist; esclist = esclist->next)
			fcache_hostescalation(fp, esclist->object_ptr);
	}

	fclose(fp);

	return OK;
}
