#ifndef INCLUDE_objects_contact_h__
#define INCLUDE_objects_contact_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"

NAGIOS_BEGIN_DECL

#define MAX_CONTACT_ADDRESSES                   6

struct contact;
typedef struct contact contact;
struct contactsmember;
typedef struct contactsmember contactsmember;

extern struct contact *contact_list;
extern struct contact **contact_ary;

struct contact {
	unsigned int id;
	char	*name;
	char	*alias;
	char	*email;
	char	*pager;
	char    *address[MAX_CONTACT_ADDRESSES];
	struct commandsmember *host_notification_commands;
	struct commandsmember *service_notification_commands;
	unsigned int host_notification_options;
	unsigned int service_notification_options;
	unsigned int minimum_value;
	char	*host_notification_period;
	char	*service_notification_period;
	int     host_notifications_enabled;
	int     service_notifications_enabled;
	int     can_submit_commands;
	int     retain_status_information;
	int     retain_nonstatus_information;
	struct customvariablesmember *custom_variables;
	time_t  last_host_notification;
	time_t  last_service_notification;
	unsigned long modified_attributes;
	unsigned long modified_host_attributes;
	unsigned long modified_service_attributes;
	struct timeperiod *host_notification_period_ptr;
	struct timeperiod *service_notification_period_ptr;
	struct objectlist *contactgroups_ptr;
	struct	contact *next;
};

struct contactsmember {
	char    *contact_name;
	struct contact *contact_ptr;
	struct contactsmember *next;
};

int init_objects_contact(int elems);
void destroy_objects_contact(void);

struct contact *create_contact(const char *name);
int setup_contact_variables(contact *new_contact, const char *alias, const char *email, const char *pager, char * const *addresses, const char *svc_notification_period, const char *host_notification_period, int service_notification_options, int host_notification_options, int service_notifications_enabled, int host_notifications_enabled, int can_submit_commands, int retain_status_information, int retain_nonstatus_information, unsigned int minimum_value);
int register_contact(contact *new_contact);
void destroy_contact(contact *this_contact);
struct commandsmember *add_service_notification_command_to_contact(contact *, char *);
struct commandsmember *add_host_notification_command_to_contact(contact *, char *);
struct customvariablesmember *add_custom_variable_to_contact(contact *, char *, char *);
struct contactsmember *add_contact_to_object(contactsmember **, char *);

struct contact *find_contact(const char *);

void fcache_contactlist(FILE *fp, const char *prefix, const struct contactsmember *list);
void fcache_contact(FILE *fp, const struct contact *temp_contact);

NAGIOS_END_DECL
#endif
