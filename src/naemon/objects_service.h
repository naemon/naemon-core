#ifndef INCLUDE_objects_service_h__
#define INCLUDE_objects_service_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_contact.h"
#include "objects_host.h"

NAGIOS_BEGIN_DECL

struct service;
typedef struct service service;
struct servicemember;
typedef struct servicesmember servicesmember;

extern struct service *service_list;
extern struct service **service_ary;

struct service {
	unsigned int id;
	char	*host_name;
	char	*description;
	char    *display_name;
	struct servicesmember *parents;
	struct servicesmember *children;
	char    *check_command;
	char    *event_handler;
	int     initial_state;
	double	check_interval;
	double  retry_interval;
	int	max_attempts;
	struct contactgroupsmember *contact_groups;
	struct contactsmember *contacts;
	double	notification_interval;
	double  first_notification_delay;
	unsigned int notification_options;
	unsigned int stalking_options;
	unsigned int hourly_value;
	int     is_volatile;
	char	*notification_period;
	char	*check_period;
	int     flap_detection_enabled;
	double  low_flap_threshold;
	double  high_flap_threshold;
	unsigned int flap_detection_options;
	int     process_performance_data;
	int     check_freshness;
	int     freshness_threshold;
	int     accept_passive_checks;
	int     event_handler_enabled;
	int	checks_enabled;
	const char *check_source;
	int     retain_status_information;
	int     retain_nonstatus_information;
	int     notifications_enabled;
	int     obsess;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	char    *icon_image;
	char    *icon_image_alt;
	struct customvariablesmember *custom_variables;
	int     problem_has_been_acknowledged;
	int     acknowledgement_type;
	int     host_problem_at_last_check;
	int     check_type;
	int	current_state;
	int	last_state;
	int	last_hard_state;
	char	*plugin_output;
	char    *long_plugin_output;
	char    *perf_data;
	int     state_type;
	time_t	next_check;
	time_t	last_check;
	int	current_attempt;
	unsigned long current_event_id;
	unsigned long last_event_id;
	unsigned long current_problem_id;
	unsigned long last_problem_id;
	time_t	last_notification;
	time_t  next_notification;
	int     no_more_notifications;
	int     check_flapping_recovery_notification;
	time_t	last_state_change;
	time_t	last_hard_state_change;
	time_t  last_time_ok;
	time_t  last_time_warning;
	time_t  last_time_unknown;
	time_t  last_time_critical;
	int     has_been_checked;
	int     is_being_freshened;
	unsigned int notified_on;
	int     current_notification_number;
	unsigned long current_notification_id;
	double  latency;
	double  execution_time;
	int     is_executing;
	int     check_options;
	int     scheduled_downtime_depth;
	int     pending_flex_downtime; /* UNUSED */
	int     state_history[MAX_STATE_HISTORY_ENTRIES];    /* flap detection */
	int     state_history_index;
	int     is_flapping;
	unsigned long flapping_comment_id;
	double  percent_state_change;
	unsigned long modified_attributes;
	struct host *host_ptr;
	struct command *event_handler_ptr;
	char *event_handler_args;
	struct command *check_command_ptr;
	struct timeperiod *check_period_ptr;
	struct timeperiod *notification_period_ptr;
	struct objectlist *servicegroups_ptr;
	struct objectlist *exec_deps, *notify_deps;
	struct objectlist *escalation_list;
	struct service *next;
	struct timed_event *next_check_event;
};

struct servicesmember {
	char    *host_name;
	char    *service_description;
	struct service *service_ptr;
	struct servicesmember *next;
};

static const struct flag_map service_flag_map[] = {
	{ OPT_WARNING, 'w', "warning" },
	{ OPT_UNKNOWN, 'u', "unknown" },
	{ OPT_CRITICAL, 'c', "critical" },
	{ OPT_FLAPPING, 'f', "flapping" },
	{ OPT_DOWNTIME, 's', "downtime" },
	{ OPT_OK, 'o', "ok" },
	{ OPT_RECOVERY, 'r', "recovery" },
	{ OPT_PENDING, 'p', "pending" },
	{ 0, 0, NULL },
};

int init_objects_service(int elems);
void destroy_objects_service(void);

service *create_service(host *hst, const char *description);
int setup_service_variables(service *svc, const char *display_name, const char *check_period, const char *check_command, int initial_state, int max_attempts, int accept_passive_checks, double check_interval, double retry_interval, double notification_interval, double first_notification_delay, char *notification_period, int notification_options, int notifications_enabled, int is_volatile, const char *event_handler, int event_handler_enabled, int checks_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, const char *notes, const char *notes_url, const char *action_url, const char *icon_image, const char *icon_image_alt, int retain_status_information, int retain_nonstatus_information, int obsess, unsigned int hourly_value);
int register_service(service *new_service);
void destroy_service(service *svc);

struct contactgroupsmember *add_contactgroup_to_service(service *, char *);					/* adds a contact group to a service definition */
struct contactsmember *add_contact_to_service(service *, char *);                                              /* adds a contact to a host definition */
struct servicesmember *add_parent_to_service(service *svc, service *parent);
struct customvariablesmember *add_custom_variable_to_service(service *, char *, char *);                       /* adds a custom variable to a service definition */

struct service *find_service(const char *, const char *);
int is_contact_for_service(struct service *, struct contact *);		       /* tests whether or not a contact is a contact member for a specific service */
int is_escalated_contact_for_service(struct service *, struct contact *);             /* checks whether or not a contact is an escalated contact for a specific service */
int get_service_count(void);
const char *service_state_name(int state);

int log_service_event(service *);
int log_service_states(int, time_t *);
void fcache_service(FILE *fp, const struct service *temp_service);

NAGIOS_END_DECL
#endif
