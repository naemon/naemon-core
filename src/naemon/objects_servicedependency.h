#ifndef INCLUDE_objects_servicedependency_h__
#define INCLUDE_objects_servicedependency_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_service.h"

NAGIOS_BEGIN_DECL

struct servicedependency;
typedef struct servicedependency servicedependency;

struct servicedependency {
	unsigned int id;
	int     dependency_type;
	char    *dependent_host_name;
	char    *dependent_service_description;
	char    *host_name;
	char    *service_description;
	char    *dependency_period;
	int     inherits_parent;
	int     failure_options;
	struct service *master_service_ptr;
	struct service *dependent_service_ptr;
	struct timeperiod *dependency_period_ptr;
};

struct servicedependency *add_service_dependency(char *dependent_host_name, char *dependent_service_description, char *host_name, char *service_description, int dependency_type, int inherits_parent, int failure_options, char *dependency_period);
void destroy_servicedependency(servicedependency *this_servicedependency);

void fcache_servicedependency(FILE *fp, const struct servicedependency *temp_servicedependency);
NAGIOS_END_DECL
#endif
