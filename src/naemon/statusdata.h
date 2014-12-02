#ifndef _STATUSDATA_H
#define _STATUSDATA_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "common.h"
#include "objects.h"

NAGIOS_BEGIN_DECL
/* Convert the (historically ordered) host states into a notion of "urgency".
	  This is defined as, in ascending order:
		SD_HOST_UP			(business as usual)
		HOST_PENDING		(waiting for - supposedly first - check result)
		SD_HOST_UNREACHABLE	(a problem, but likely not its cause)
		SD_HOST_DOWN		(look here!!)
	  The exact values are irrelevant, so I try to make the conversion as
	  CPU-efficient as possible: */
#define HOST_URGENCY(hs)		((hs)|(((hs)&0x5)<<1))

int initialize_status_data(const char *);               /* initializes status data at program start */
int update_all_status_data(void);                       /* updates all status data */
int cleanup_status_data(int);                           /* cleans up status data at program termination */
int update_program_status(int);                         /* updates program status data */
int update_host_status(host *, int);                    /* updates host status data */
int update_service_status(service *, int);              /* updates service status data */
int update_contact_status(contact *, int);              /* updates contact status data */

NAGIOS_END_DECL
#endif
