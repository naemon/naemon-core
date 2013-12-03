#include "config.h"
#include "common.h"
#include "objects.h"
#include "statusdata.h"
#include "nagios.h"
#include "sretention.h"
#include "broker.h"
#include "xrddefault.h"


/******************************************************************/
/************* TOP-LEVEL STATE INFORMATION FUNCTIONS **************/
/******************************************************************/

/* initializes retention data at program start */
int initialize_retention_data(const char *cfgfile)
{
	return xrddefault_initialize_retention_data(cfgfile);
}


/* cleans up retention data before program termination */
int cleanup_retention_data(void)
{
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

	if (result == ERROR)
		return ERROR;

	return OK;
}
