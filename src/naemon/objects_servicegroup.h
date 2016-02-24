#ifndef INCLUDE_objects_servicegroup_h__
#define INCLUDE_objects_servicegroup_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <stdio.h>
#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_service.h"

NAGIOS_BEGIN_DECL

struct servicegroup;
typedef struct servicegroup servicegroup;

extern struct servicegroup *servicegroup_list;
extern struct servicegroup **servicegroup_ary;

struct servicegroup {
	unsigned int id;
	char	*group_name;
	char    *alias;
	struct servicesmember *members;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	struct	servicegroup *next;
};

int init_objects_servicegroup(int elems);
void destroy_objects_servicegroup(void);

servicegroup *create_servicegroup(const char *name, const char *alias, const char *notes, const char *notes_url, const char *action_url);
int register_servicegroup(servicegroup *this_servicegroup);
void destroy_servicegroup(servicegroup *this_servicegroup);
struct servicesmember *add_service_to_servicegroup(servicegroup *, service *);
void remove_service_from_servicegroup(servicegroup *temp_servicegroup, service *svc);

struct servicegroup *find_servicegroup(const char *);
int is_host_member_of_servicegroup(struct servicegroup *, struct host *);	       /* tests whether or not a service is a member of a specific servicegroup */
int is_service_member_of_servicegroup(struct servicegroup *, struct service *);	/* tests whether or not a service is a member of a specific servicegroup */

void fcache_servicegroup(FILE *fp, const struct servicegroup *temp_servicegroup);

NAGIOS_END_DECL
#endif
