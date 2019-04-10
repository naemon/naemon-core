#ifndef INCLUDE_objects_contactgroup_h__
#define INCLUDE_objects_contactgroup_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/lnae-utils.h"
#include "objects_contact.h"

NAGIOS_BEGIN_DECL
struct contactgroup;
typedef struct contactgroup contactgroup;
struct contactgroupsmember;
typedef struct contactgroupsmember contactgroupsmember;

extern struct contactgroup *contactgroup_list;
extern struct contactgroup **contactgroup_ary;

struct contactgroup {
	unsigned int id;
	char	*group_name;
	char    *alias;
	struct contactsmember *members;
	struct contactgroup *next;
};

struct contactgroupsmember {
	char    *group_name;
	struct contactgroup *group_ptr;
	struct contactgroupsmember *next;
};

int init_objects_contactgroup(int elems);
void destroy_objects_contactgroup(void);

struct contactgroup *create_contactgroup(const char *, const char *);
int register_contactgroup(contactgroup *new_contactgroup);
void destroy_contactgroup(contactgroup *this_contactgroup);
contactgroupsmember *add_contactgroup_to_object(contactgroupsmember **cg_list, const char *group_name);
struct contactsmember *add_contact_to_contactgroup(contactgroup *, char *);

struct contactgroup *find_contactgroup(const char *);
int is_contact_member_of_contactgroup(struct contactgroup *, struct contact *);	/* tests whether or not a contact is a member of a specific contact group */

void fcache_contactgrouplist(FILE *fp, const char *prefix, const struct contactgroupsmember *list);
void fcache_contactgroup(FILE *fp, const struct contactgroup *temp_contactgroup);
NAGIOS_END_DECL
#endif
