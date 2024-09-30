#include "config.h"
#include "common.h"
#include "statusdata.h"
#include "sretention.h"
#include "broker.h"
#include "xrddefault.h"
#include "globals.h"
#include "logging.h"
#include "nm_alloc.h"
#include "events.h"
#include <string.h>

/* hosts and services before attribute modifications */
static struct host **premod_hosts;
static struct service **premod_services;
static struct contact **premod_contacts;

/******************************************************************/
/************* TOP-LEVEL STATE INFORMATION FUNCTIONS **************/
/******************************************************************/

void save_state_information_eventhandler(struct nm_event_execution_properties *evprop)
{
	int status;

	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
		schedule_event((time_t)retention_update_interval * interval_length, save_state_information_eventhandler, evprop->user_data);

		status = save_state_information(TRUE);

		if (status == OK) {
			nm_log(NSLOG_PROCESS_INFO,
			       "Auto-save of retention data completed successfully.\n");
		}
	}
}

/* initializes retention data at program start */
int initialize_retention_data()
{
	premod_hosts = nm_calloc(num_objects.hosts, sizeof(void *));
	premod_services = nm_calloc(num_objects.services, sizeof(void *));
	premod_contacts = nm_calloc(num_objects.contacts, sizeof(void *));

	/* add a retention data save event if needed */
	if (retain_state_information == TRUE && retention_update_interval > 0)
		schedule_event((time_t)retention_update_interval * interval_length, save_state_information_eventhandler, NULL);

	return xrddefault_initialize_retention_data();
}


/* cleans up retention data before program termination */
int cleanup_retention_data(void)
{
	unsigned int i;

	for (i = 0; i < num_objects.hosts; i++) {
		nm_free(premod_hosts[i]);
	}
	nm_free(premod_hosts);
	for (i = 0; i < num_objects.services; i++) {
		nm_free(premod_services[i]);
	}
	nm_free(premod_services);
	for (i = 0; i < num_objects.contacts; i++) {
		nm_free(premod_contacts[i]);
	}
	nm_free(premod_contacts);

	return xrddefault_cleanup_retention_data();
}


/* save all host and service state information */
int save_state_information(int autosave)
{
	int result = OK;

	if (retain_state_information == FALSE)
		return OK;

	broker_retention_data(NEBTYPE_RETENTIONDATA_STARTSAVE, NEBFLAG_NONE, NEBATTR_NONE);

	result = xrddefault_save_state_information();

	broker_retention_data(NEBTYPE_RETENTIONDATA_ENDSAVE, NEBFLAG_NONE, NEBATTR_NONE);

	if (result == ERROR)
		return ERROR;

	if (!autosave) {
		nm_log(NSLOG_INFO_MESSAGE, "Retention data successfully saved.");
	}

	return OK;
}


/* reads in initial host and state information */
int read_initial_state_information(void)
{
	int result = OK;

	if (retain_state_information == FALSE)
		return OK;

	broker_retention_data(NEBTYPE_RETENTIONDATA_STARTLOAD, NEBFLAG_NONE, NEBATTR_NONE);

	result = xrddefault_read_state_information();

	broker_retention_data(NEBTYPE_RETENTIONDATA_ENDLOAD, NEBFLAG_NONE, NEBATTR_NONE);

	return result;
}

int pre_modify_contact_attribute(struct contact *c, int attr)
{
	struct contact *stash;

	/* might be stashed already */
	if (premod_contacts[c->id]) {
		return 0;
	}

	stash = nm_malloc(sizeof(*stash));
	memcpy(stash, c, sizeof(*stash));
	premod_contacts[c->id] = stash;
	return 0;
}

int pre_modify_service_attribute(struct service *s, int attr)
{
	struct service *stash;

	/* might be stashed already */
	if (premod_services[s->id]) {
		return 0;
	}

	stash = nm_malloc(sizeof(*stash));
	memcpy(stash, s, sizeof(*stash));
	premod_services[s->id] = stash;
	return 0;
}

int pre_modify_host_attribute(struct host *h, int attr)
{
	struct host *stash;

	/* might be stashed already */
	if (premod_hosts[h->id]) {
		return 0;
	}

	stash = nm_malloc(sizeof(*stash));
	memcpy(stash, h, sizeof(*stash));
	premod_hosts[h->id] = stash;
	return 0;
}

struct contact *get_premod_contact(unsigned int id)
{
	return premod_contacts ? premod_contacts[id] : NULL;
}

struct host *get_premod_host(unsigned int id)
{
	return premod_hosts ? premod_hosts[id] : NULL;
}

struct service *get_premod_service(unsigned int id)
{
	return premod_services ? premod_services[id] : NULL;
}
