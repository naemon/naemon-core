#ifndef INCLUDE_objects_hostgroup_h__
#define INCLUDE_objects_hostgroup_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include <glib.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_host.h"

NAGIOS_BEGIN_DECL

struct hostgroup;
typedef struct hostgroup hostgroup;

extern struct hostgroup *hostgroup_list;
extern struct hostgroup **hostgroup_ary;

struct hostgroup {
	unsigned int id;
	char	*group_name;
	char    *alias;
	GTree   *members;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	struct	hostgroup *next;
};

int init_objects_hostgroup(int elems);
void destroy_objects_hostgroup(void);

hostgroup *create_hostgroup(const char *name, const char *alias, const char *notes, const char *notes_url, const char *action_url);
int register_hostgroup(hostgroup *new_hostgroup);
void destroy_hostgroup(hostgroup *this_hostgroup);
int add_host_to_hostgroup(hostgroup *, host *);
int remove_host_from_hostgroup(hostgroup *temp_hostgroup, host *h);

struct hostgroup *find_hostgroup(const char *);
int is_host_member_of_hostgroup(struct hostgroup *, struct host *);		       /* tests whether or not a host is a member of a specific hostgroup */

void fcache_hostgroup(FILE *fp, const struct hostgroup *temp_hostgroup);

NAGIOS_END_DECL
#endif
