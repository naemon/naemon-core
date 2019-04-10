#ifndef INCLUDE_objects_serviceescalation_h__
#define INCLUDE_objects_serviceescalation_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_service.h"

NAGIOS_BEGIN_DECL

struct serviceescalation;
typedef struct serviceescalation serviceescalation;

struct serviceescalation {
	unsigned int id;
	char    *host_name;
	char    *description;
	int     first_notification;
	int     last_notification;
	double  notification_interval;
	char    *escalation_period;
	int     escalation_options;
	struct contactgroupsmember *contact_groups;
	struct contactsmember *contacts;
	struct service *service_ptr;
	struct timeperiod *escalation_period_ptr;
};

struct serviceescalation *add_serviceescalation(char *host_name, char *description, int first_notification, int last_notification, double notification_interval, char *escalation_period, int escalation_options);
void destroy_serviceescalation(serviceescalation *this_serviceescalation);
struct contactgroupsmember *add_contactgroup_to_serviceescalation(serviceescalation *, char *);                /* adds a contact group to a service escalation definition */
struct contactsmember *add_contact_to_serviceescalation(serviceescalation *, char *);                          /* adds a contact to a service escalation definition */

void fcache_serviceescalation(FILE *fp, const struct serviceescalation *temp_serviceescalation);

NAGIOS_END_DECL
#endif
