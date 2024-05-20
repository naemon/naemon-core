#include "config.h"
#include "common.h"
#include "statusdata.h"
#include "xsddefault.h"
#include "broker.h"
#include "globals.h"
#include "events.h"
#include "sehandlers.h"


/******************************************************************/
/****************** TOP-LEVEL OUTPUT FUNCTIONS ********************/
/******************************************************************/

static void update_all_status_data_eventhandler(struct nm_event_execution_properties *evprop)
{
	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
		/*
		 * if status data updates are turned off we reschedule
		 * with a short interval to avoid hammering the scheduling
		 * queue. This makes it possible to update the variable at
		 * runtime and have the new setting take effect fast-ish
		 */
		int interval = status_update_interval ? status_update_interval : 10;
		/* Reschedule, so it becomes recurring */
		schedule_event(interval, update_all_status_data_eventhandler, NULL);

		if (!status_update_interval)
			return;
		update_all_status_data();
	}
}

static void update_status_data_eventhandler(struct nm_event_execution_properties *evprop)
{
	if (evprop->execution_type == EVENT_EXEC_NORMAL) {
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

	int display_status = 0;

	display_status = hst->display_status;

	/* Downtime */
	if (hst->scheduled_downtime_depth > 0) {
			display_status = 1;
	}
	/* ACK */
	else if (hst->problem_has_been_acknowledged == TRUE)  {
			display_status = 2;
	}
	/* Flapping*/
	else if (hst->is_flapping > 0) {
			display_status = 3;
	}
	/* Unreachable */
	else if (hst->current_state == STATE_UNREACHABLE) {
			display_status = 7;
	}
	/* Down */
	else if (hst->current_state == STATE_DOWN) {
			display_status = 8;
	}
	else if (hst->current_state == STATE_OK) {
			display_status = 0;
	}

	if (display_status != hst->display_status) {
			hst->display_status = display_status;
			log_host_event(hst);
			broker_statechange_data(NEBTYPE_STATECHANGE_END, NEBFLAG_NONE, NEBATTR_NONE, HOST_STATECHANGE, (void *)hst, hst->current_state, hst->state_type, hst->current_attempt, hst->max_attempts);
	}

	if (aggregated_dump == FALSE)
		broker_host_status(NEBTYPE_HOSTSTATUS_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst);

	return OK;
}


/* updates service status info */
int update_service_status(service *svc, int aggregated_dump)
{
	int display_status;

	display_status = svc->display_status;

	/* Downtime */
	if ( svc->scheduled_downtime_depth > 0) {
			display_status = 1;
	}
	/* ACK */
	else if (svc->problem_has_been_acknowledged == TRUE)  {
			display_status = 2;
	}
	/* Flapping*/
	else if (svc->is_flapping > 0) {
			display_status = 3;
	}
	/* Warning */
	else if (svc->current_state == STATE_WARNING) {
			display_status = 4;
	}
	/* Unknown */
	else if (svc->current_state == STATE_UNKNOWN) {
			display_status = 5;
	}
	/* CRITICAL */
	else if (svc->current_state == STATE_CRITICAL) {
			display_status = 6;
	}
	else if (svc->current_state == STATE_OK) {
			display_status = 0;
	}

	if (display_status != svc->display_status) {
			svc->display_status = display_status;
			log_service_event(svc);
			broker_statechange_data(NEBTYPE_STATECHANGE_END, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_STATECHANGE, (void *)svc, svc->current_state, svc->state_type, svc->current_attempt, svc->max_attempts);
	}

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
