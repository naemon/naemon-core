#ifndef GLOBALS_H_
#define GLOBALS_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "common.h"
#include "objectlist.h"
#include "objects_command.h"
#include "macros.h" /* For MAX_USER_MACROS */

NAGIOS_BEGIN_DECL

/*
 * global variables only used in the core. Reducing this list would be
 * a Good Thing(tm).
 */
extern char *naemon_binary_path;
extern char *config_file;
extern char *command_file;
extern char *temp_file;
extern char *temp_path;
extern char *check_result_path;
extern char *lock_file;
extern char *object_precache_file;
extern objectlist *objcfg_files;
extern objectlist *objcfg_dirs;

extern unsigned int nofile_limit, nproc_limit, max_apps;

extern int num_check_workers;
extern char *qh_socket_path;

extern char *macro_user[MAX_USER_MACROS];

extern char *ocsp_command;
extern char *ochp_command;
extern command *ocsp_command_ptr;
extern command *ochp_command_ptr;
extern int ocsp_timeout;
extern int ochp_timeout;

extern char *global_host_event_handler;
extern char *global_service_event_handler;
extern command *global_host_event_handler_ptr;
extern command *global_service_event_handler_ptr;

extern int use_regexp_matches;
extern int use_true_regexp_matching;

extern int use_syslog;
extern char *log_file;
extern char *log_archive_path;
extern int log_notifications;
extern int enable_notification_suppression_reason_logging;
extern int log_service_retries;
extern int log_host_retries;
extern int log_event_handlers;
extern int log_external_commands;
extern int log_passive_checks;
extern unsigned long logging_options;
extern unsigned long syslog_options;

extern int service_check_timeout;
extern int service_check_timeout_state;
extern int host_check_timeout;
extern int event_handler_timeout;
extern int notification_timeout;

extern volatile sig_atomic_t sig_id;

extern int verify_config;
extern int precache_objects;
extern int use_precached_objects;

extern int max_parallel_service_checks;

extern int check_reaper_interval;
extern int max_check_reaper_time;
extern int service_freshness_check_interval;
extern int host_freshness_check_interval;
extern int auto_rescheduling_interval;
extern int auto_rescheduling_window;

extern int check_orphaned_services;
extern int check_orphaned_hosts;
extern int check_service_freshness;
extern int check_host_freshness;

extern int additional_freshness_latency;

extern int check_for_updates;
extern int bare_update_check;
extern time_t last_update_check;
extern unsigned long update_uid;
extern int update_available;
extern char *last_program_version;
extern char *new_program_version;

extern int use_aggressive_host_checking;
extern time_t cached_host_check_horizon;
extern time_t cached_service_check_horizon;
extern int enable_predictive_host_dependency_checks;
extern int enable_predictive_service_dependency_checks;

extern int soft_state_dependencies;

extern int retain_state_information;
extern int retention_update_interval;
extern int use_retained_program_state;
extern int use_retained_scheduling_info;
extern int retained_scheduling_randomize_window;
extern int retention_scheduling_horizon;
extern char *retention_file;
extern unsigned long retained_host_attribute_mask;
extern unsigned long retained_service_attribute_mask;
extern unsigned long retained_contact_host_attribute_mask;
extern unsigned long retained_contact_service_attribute_mask;
extern unsigned long retained_process_host_attribute_mask;
extern unsigned long retained_process_service_attribute_mask;

extern int translate_passive_host_checks;
extern int passive_host_checks_are_soft;

extern int status_update_interval;

extern int time_change_threshold;

extern unsigned long event_broker_options;

extern double low_service_flap_threshold;
extern double high_service_flap_threshold;
extern double low_host_flap_threshold;
extern double high_host_flap_threshold;

extern char *use_timezone;

extern time_t max_check_result_file_age;

extern char *debug_file;
extern int debug_level;
extern int debug_verbosity;
extern unsigned long max_debug_file_size;

extern int allow_empty_hostgroup_assignment;
extern int allow_circular_dependencies;
extern int host_down_disable_service_checks;
extern int service_skip_check_dependency_status;
extern int service_skip_check_parent_status;
extern int service_skip_check_host_down_status;
extern int host_skip_check_dependency_status;

extern time_t last_program_stop;
extern time_t event_start;

extern volatile sig_atomic_t sigshutdown, sigrestart, sigrotate, sigfilesize;
extern int currently_running_service_checks;
extern int currently_running_host_checks;

extern unsigned long next_event_id;
extern unsigned long next_problem_id;
extern unsigned long next_comment_id;
extern unsigned long next_notification_id;

extern unsigned long modified_process_attributes;
extern unsigned long modified_host_process_attributes;
extern unsigned long modified_service_process_attributes;

extern struct check_engine nagios_check_engine;

NAGIOS_END_DECL

#endif
