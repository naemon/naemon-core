#ifndef INCLUDE_objects_hostescalation_h__
#define INCLUDE_objects_hostescalation_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_host.h"

NAGIOS_BEGIN_DECL

struct hostescalation;
typedef struct hostescalation hostescalation;

struct hostescalation {
	unsigned int id;
	char    *host_name;
	int     first_notification;
	int     last_notification;
	double  notification_interval;
	char    *escalation_period;
	int     escalation_options;
	struct contactgroupsmember *contact_groups;
	struct contactsmember *contacts;
	struct host    *host_ptr;
	struct timeperiod *escalation_period_ptr;
};

struct hostescalation *add_hostescalation(char *host_name, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options);
void destroy_hostescalation(hostescalation *this_hostescalation);
struct contactsmember *add_contact_to_hostescalation(hostescalation *, char *);                                /* adds a contact to a host escalation definition */
struct contactgroupsmember *add_contactgroup_to_hostescalation(hostescalation *, char *);                      /* adds a contact group to a host escalation definition */

void fcache_hostescalation(FILE *fp, const struct hostescalation *temp_hostescalation);

NAGIOS_END_DECL
#endif
