#ifndef INCLUDE_objects_hostdependency_h__
#define INCLUDE_objects_hostdependency_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_host.h"

NAGIOS_BEGIN_DECL

struct hostdependency;
typedef struct hostdependency hostdependency;

struct hostdependency {
	unsigned int id;
	int     dependency_type;
	char    *dependent_host_name;
	char    *host_name;
	char    *dependency_period;
	int     inherits_parent;
	int     failure_options;
	struct host    *master_host_ptr;
	struct host    *dependent_host_ptr;
	struct timeperiod *dependency_period_ptr;
};

struct hostdependency *add_host_dependency(char *dependent_host_name, char *host_name, int dependency_type, int inherits_parent, int failure_options, char *dependency_period);
void destroy_hostdependency(hostdependency *this_hostdependency);

void fcache_hostdependency(FILE *fp, const struct hostdependency *temp_hostdependency);

NAGIOS_END_DECL
#endif
