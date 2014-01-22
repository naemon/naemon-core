#include "config.h"
#include "common.h"
#include "objects.h"
#include "statusdata.h"
#include "sretention.h"
#include "broker.h"
#include "xrddefault.h"
#include "globals.h"
#include "logging.h"
#include <string.h>

/* hosts and services before attribute modifications */
static struct host **premod_hosts;
static struct service **premod_services;

/******************************************************************/
/************* TOP-LEVEL STATE INFORMATION FUNCTIONS **************/
/******************************************************************/

/* initializes retention data at program start */
int initialize_retention_data(const char *cfgfile)
{
	if (!(premod_hosts = calloc(sizeof(void *), num_objects.hosts)))
		return ERROR;
	if (!(premod_services = calloc(sizeof(void *), num_objects.services))) {
		free(premod_hosts);
		return ERROR;
	}

	return xrddefault_initialize_retention_data(cfgfile);
}


/* cleans up retention data before program termination */
int cleanup_retention_data(void)
{
	unsigned int i;

	for (i = 0; i < num_objects.hosts; i++) {
		free(premod_hosts[i]);
	}
	for (i = 0; i < num_objects.services; i++) {
		free(premod_services[i]);
	}
	premod_hosts = NULL;
	premod_services = NULL;

	return xrddefault_cleanup_retention_data();
}


/* save all host and service state information */
int save_state_information(int autosave)
{
	int result = OK;

	if (retain_state_information == FALSE)
		return OK;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_retention_data(NEBTYPE_RETENTIONDATA_STARTSAVE, NEBFLAG_NONE, NEBATTR_NONE, NULL);
#endif

	result = xrddefault_save_state_information();

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_retention_data(NEBTYPE_RETENTIONDATA_ENDSAVE, NEBFLAG_NONE, NEBATTR_NONE, NULL);
#endif

	if (result == ERROR)
		return ERROR;

	if (autosave == TRUE)
		logit(NSLOG_PROCESS_INFO, FALSE, "Auto-save of retention data completed successfully.\n");

	return OK;
}


/* reads in initial host and state information */
int read_initial_state_information(void)
{
	int result = OK;

	if (retain_state_information == FALSE)
		return OK;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_retention_data(NEBTYPE_RETENTIONDATA_STARTLOAD, NEBFLAG_NONE, NEBATTR_NONE, NULL);
#endif

	result = xrddefault_read_state_information();

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_retention_data(NEBTYPE_RETENTIONDATA_ENDLOAD, NEBFLAG_NONE, NEBATTR_NONE, NULL);
#endif

	return result;
}

int pre_modify_service_attribute(struct service *s, int attr)
{
	struct service *stash;

	/* might be stashed already */
	if (premod_services[s->id]) {
		return 0;
	}

	stash = malloc(sizeof(*stash));
	memcpy(stash, s, sizeof(*stash));
	premod_services[s->id] = stash;
	return 0;
}

int pre_modify_host_attribute(struct host *h, int attr)
{
	struct host *stash;

	/* might be stashed already */
	if (premod_hosts[h->id]) {
		printf("Host '%s' already stashed. Skipping\n", h->name);
		return 0;
	}

	printf("Stashing host '%s'\n", h->name);
	stash = malloc(sizeof(*stash));
	memcpy(stash, h, sizeof(*stash));
	premod_hosts[h->id] = stash;
	return 0;
}

struct host *get_premod_host(unsigned int id)
{
	return premod_hosts ? premod_hosts[id] : NULL;
}

struct service *get_premod_service(unsigned int id)
{
	return premod_services ? premod_services[id] : NULL;
}
