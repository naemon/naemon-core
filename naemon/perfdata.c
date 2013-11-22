#include "config.h"
#include "common.h"
#include "objects.h"
#include "perfdata.h"
#include "macros.h"
#include "xpddefault.h"


/******************************************************************/
/************** INITIALIZATION & CLEANUP FUNCTIONS ****************/
/******************************************************************/

/* initializes performance data */
int initialize_performance_data(const char *cfgfile) {
	return xpddefault_initialize_performance_data(cfgfile);
	}



/* cleans up performance data */
int cleanup_performance_data(void) {
	return xpddefault_cleanup_performance_data();
	}



/******************************************************************/
/****************** PERFORMANCE DATA FUNCTIONS ********************/
/******************************************************************/


/* updates service performance data */
int update_service_performance_data(service *svc) {

	/* should we be processing performance data for anything? */
	if(process_performance_data == FALSE)
		return OK;

	/* should we process performance data for this service? */
	if(svc->process_performance_data == FALSE)
		return OK;

	/* process the performance data! */
	xpddefault_update_service_performance_data(svc);

	return OK;
	}



/* updates host performance data */
int update_host_performance_data(host *hst) {

	/* should we be processing performance data for anything? */
	if(process_performance_data == FALSE)
		return OK;

	/* should we process performance data for this host? */
	if(hst->process_performance_data == FALSE)
		return OK;

	/* process the performance data! */
	xpddefault_update_host_performance_data(hst);

	return OK;
	}
