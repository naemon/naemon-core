#include "config.h"
#include "common.h"
#include "statusdata.h"
#include "xsddefault.h"
#include "broker.h"
#include "globals.h"
#include "events.h"


/******************************************************************/
/****************** TOP-LEVEL OUTPUT FUNCTIONS ********************/
/******************************************************************/

static void update_all_status_data_eventhandler(struct nm_event_execution_properties *evprop)
{
	if(evprop->execution_type == EVENT_EXEC_NORMAL) {
		/* Reschedule, so it becomes recurring */
		schedule_event(status_update_interval, update_all_status_data_eventhandler, NULL);

		update_all_status_data();
	}
}

static void update_status_data_eventhandler(struct nm_event_execution_properties *evprop)
{
	if(evprop->execution_type == EVENT_EXEC_NORMAL) {
		/* Reschedule, so it becomes recurring */
		schedule_event(5, update_status_data_eventhandler, NULL);
		update_program_status(FALSE);
	}
}

/* initializes status data at program start */
int initialize_status_data(const char *cfgfile)
{
	/* add a status save event */
	schedule_event(status_update_interval, update_all_status_data_eventhandler, NULL);
	schedule_event(5, update_status_data_eventhandler, NULL);

	return xsddefault_initialize_status_data(cfgfile);
}


/* update all status data (aggregated dump) */
int update_all_status_data(void)
{
	int result = OK;

	broker_aggregated_status_data(NEBTYPE_AGGREGATEDSTATUS_STARTDUMP, NEBFLAG_NONE, NEBATTR_NONE);

	result = xsddefault_save_status_data();

	broker_aggregated_status_data(NEBTYPE_AGGREGATEDSTATUS_ENDDUMP, NEBFLAG_NONE, NEBATTR_NONE);
	return result;
}


/* cleans up status data before program termination */
int cleanup_status_data(int delete_status_data)
{
	return xsddefault_cleanup_status_data(delete_status_data);
}


/* updates program status info */
int update_program_status(int aggregated_dump)
{

	if (aggregated_dump == FALSE)
		broker_program_status(NEBTYPE_PROGRAMSTATUS_UPDATE, NEBFLAG_NONE, NEBATTR_NONE);

	return OK;
}


/* updates host status info */
int update_host_status(host *hst, int aggregated_dump)
{

	if (aggregated_dump == FALSE)
		broker_host_status(NEBTYPE_HOSTSTATUS_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst);

	return OK;
}


/* updates service status info */
int update_service_status(service *svc, int aggregated_dump)
{

	if (aggregated_dump == FALSE)
		broker_service_status(NEBTYPE_SERVICESTATUS_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc);

	return OK;
}


/* updates contact status info */
int update_contact_status(contact *cntct, int aggregated_dump)
{

	if (aggregated_dump == FALSE)
		broker_contact_status(NEBTYPE_CONTACTSTATUS_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, cntct);

	return OK;
}
