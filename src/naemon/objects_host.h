#ifndef INCLUDE_objects_host_h__
#define INCLUDE_objects_host_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <stdio.h>
#include <time.h>
#include <glib.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"
#include "objects_contact.h"

NAGIOS_BEGIN_DECL

struct host;
typedef struct host host;
struct servicesmember;

extern struct host **host_ary;
extern struct host *host_list;

struct host {
	unsigned int id;
	char    *name;
	char    *display_name;
	char	*alias;
	char    *address;
	GTree   *parent_hosts; /* char * => struct host * */
	GTree   *child_hosts; /* char * => struct host * */
	struct servicesmember *services;
	char    *check_command;
	int     initial_state;
	double  check_interval;
	double  retry_interval;
	int     max_attempts;
	char    *event_handler;
	struct contactgroupsmember *contact_groups;
	struct contactsmember *contacts;
	double  notification_interval;
	double  first_notification_delay;
	unsigned int notification_options;
	unsigned int hourly_value;
	char	*notification_period;
	char    *check_period;
	int     flap_detection_enabled;
	double  low_flap_threshold;
	double  high_flap_threshold;
	int     flap_detection_options;
	unsigned int stalking_options;
	int     check_freshness;
	int     freshness_threshold;
	int     process_performance_data;
	int     checks_enabled;
	const char *check_source;
	int     accept_passive_checks;
	int     event_handler_enabled;
	int     retain_status_information;
	int     retain_nonstatus_information;
	int     obsess;
	char    *notes;
	char    *notes_url;
	char    *action_url;
	char    *icon_image;
	char    *icon_image_alt;
	char    *statusmap_image; /* used by lots of graphing tools */
	char    *vrml_image;
	int     have_2d_coords;
	int     x_2d;
	int     y_2d;
	int     have_3d_coords;
	double  x_3d;
	double  y_3d;
	double  z_3d;
	customvariablesmember *custom_variables;
	int     problem_has_been_acknowledged;
	int     acknowledgement_type;
	int     check_type;
	int     current_state;
	int     last_state;
	int     last_hard_state;
	char	*plugin_output;
	char    *long_plugin_output;
	char    *perf_data;
	int     state_type;
	int     current_attempt;
	unsigned long current_event_id;
	unsigned long last_event_id;
	unsigned long current_problem_id;
	unsigned long last_problem_id;
	double  latency;
	double  execution_time;
	int     is_executing;
	int     check_options;
	int     notifications_enabled;
	time_t  last_notification;
	time_t  next_notification;
	time_t  next_check;
	time_t  last_check;
	time_t	last_state_change;
	time_t	last_hard_state_change;
	time_t  last_time_up;
	time_t  last_time_down;
	time_t  last_time_unreachable;
	int     has_been_checked;
	int     is_being_freshened;
	int     notified_on;
	int     current_notification_number;
	int     no_more_notifications;
	unsigned long current_notification_id;
	int     check_flapping_recovery_notification;
	int     scheduled_downtime_depth;
	int     pending_flex_downtime; /* UNUSED */
	int     state_history[MAX_STATE_HISTORY_ENTRIES];    /* flap detection */
	int     state_history_index;
	time_t  last_state_history_update;
	int     is_flapping;
	unsigned long flapping_comment_id;
	double  percent_state_change;
	int     total_services;
	unsigned long modified_attributes;
	struct command *event_handler_ptr;
	struct command *check_command_ptr;
	struct timeperiod *check_period_ptr;
	struct timeperiod *notification_period_ptr;
	struct objectlist *hostgroups_ptr;
	/* objects we depend upon */
	struct objectlist *exec_deps, *notify_deps;
	struct objectlist *escalation_list;
	struct  host *next;
	struct timed_event *next_check_event;
};

static const struct flag_map host_flag_map[] = {
	{ OPT_DOWN, 'd', "down" },
	{ OPT_UNREACHABLE, 'u', "unreachable" },
	{ OPT_FLAPPING, 'f', "flapping" },
	{ OPT_RECOVERY, 'r', "recovery" },
	{ OPT_DOWNTIME, 's', "downtime" },
	{ OPT_PENDING, 'p', "pending" },
	{ 0, 0, NULL },
};

int init_objects_host(int elems);
void destroy_objects_host(void);

host *create_host(const char *name);
/** This is an evil legacy function which you should never, ever use */
int setup_host_variables(host *new_host, const char *display_name, const char *alias, const char *address, const char *check_period, int initial_state, double check_interval, double retry_interval, int max_attempts, int notification_options, double notification_interval, double first_notification_delay, const char *notification_period, int notifications_enabled, const char *check_command, int checks_enabled, int accept_passive_checks, const char *event_handler, int event_handler_enabled, int flap_detection_enabled, double low_flap_threshold, double high_flap_threshold, int flap_detection_options, int stalking_options, int process_perfdata, int check_freshness, int freshness_threshold, const char *notes, const char *notes_url, const char *action_url, const char *icon_image, const char *icon_image_alt, const char *vrml_image, const char *statusmap_image, int x_2d, int y_2d, int have_2d_coords, double x_3d, double y_3d, double z_3d, int have_3d_coords, int retain_status_information, int retain_nonstatus_information, int obsess_over_host, unsigned int hourly_value);
int register_host(host *new_host);
void destroy_host(host *this_host);

int add_parent_to_host(host *, host *);
int remove_parent_from_host(host *hst, host *parent);
struct contactgroupsmember *add_contactgroup_to_host(host *, char *);
struct contactsmember *add_contact_to_host(host *, char *);
struct customvariablesmember *add_custom_variable_to_host(host *, char *, char *);

int compare_host(const void *_host1, const void *_host2);
struct host *find_host(const char *);
int is_contact_for_host(struct host *, struct contact *);
int is_escalated_contact_for_host(struct host *, struct contact *);
int number_of_immediate_child_hosts(struct host *);
int number_of_total_child_hosts(struct host *);
int number_of_immediate_parent_hosts(struct host *);


const char *host_state_name(int state);
int get_host_count(void);
unsigned int host_services_value(struct host *h);

char * implode_hosttree(GTree *tree, char *delimiter);
void fcache_host(FILE *fp, const struct host *temp_host);

int log_host_event(host *);
int log_host_states(int, time_t *);

NAGIOS_END_DECL
#endif
