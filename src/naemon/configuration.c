#include "config.h"
#include "common.h"
#include "objects.h"
#include "objectlist.h"
#include "objects_command.h"
#include "objects_hostdependency.h"
#include "objects_servicedependency.h"
#include "xodtemplate.h"
#include "macros.h"
#include "broker.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "utils.h"
#include "configuration.h"
#include "events.h"
#include "logging.h"
#include "globals.h"
#include "perfdata.h"
#include "nm_alloc.h"
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

static objectlist *maincfg_files = NULL;
static objectlist *maincfg_dirs = NULL;

/******************************************************************/
/************** CONFIGURATION INPUT FUNCTIONS *********************/
/******************************************************************/

/* read all configuration data */
int read_all_object_data(const char *main_config_file)
{
	memset(&num_objects, 0, sizeof(num_objects));
	return xodtemplate_read_config_data(main_config_file);
}


static objectlist *deprecated = NULL;
static void obsoleted_warning(const char *key, const char *msg)
{
	char *buf;
	nm_asprintf(&buf, "Warning: %s is deprecated and will be removed.%s%s\n",
	            key, msg ? " " : "", msg ? msg : "");
	prepend_object_to_objectlist(&deprecated, buf);
}

static int
read_config_file(const char *main_config_file, nagios_macros *mac)
{
	int error = FALSE;
	char *temp_ptr = NULL;
	int current_line = 0;
	char *error_message = NULL;
	char *value = NULL;
	char *input = NULL;
	char *variable = NULL;
	char *modptr = NULL;
	char *argptr = NULL;
	mmapfile *thefile = NULL;
	DIR *tmpdir = NULL;

	/* open the config file for reading */
	if ((thefile = mmap_fopen(main_config_file)) == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Cannot open main configuration file '%s' for reading!", main_config_file);
		return ERROR;
	}

	/* process all lines in the config file */
	while (1) {

		nm_free(input);
		nm_free(variable);
		nm_free(value);

		/* read the next line */
		if ((input = mmap_fgets_multiline(thefile)) == NULL)
			break;

		current_line = thefile->current_line;

		strip(input);

		/* skip blank lines and comments */
		if (input[0] == '\x0' || input[0] == '#')
			continue;

		/* get the variable name */
		if ((temp_ptr = my_strtok(input, "=")) == NULL) {
			nm_asprintf(&error_message, "NULL variable");
			error = TRUE;
			break;
		}
		variable = nm_strdup(temp_ptr);

		/* get the value */
		if ((temp_ptr = my_strtok(NULL, "\n")) == NULL) {
			nm_asprintf(&error_message, "NULL value");
			error = TRUE;
			break;
		}
		value = (char *)nm_strdup(temp_ptr);
		strip(variable);
		strip(value);

		/* process the variable/value */

		if (!strcmp(variable, "resource_file")) {

			/* save the macro */
			nm_free(mac->x[MACRO_RESOURCEFILE]);
			mac->x[MACRO_RESOURCEFILE] = nspath_absolute(value, config_file_dir);

			/* process the resource file */
			if (read_resource_file(mac->x[MACRO_RESOURCEFILE]) == ERROR) {
				error = TRUE;
			}
		}

		else if (!strcmp(variable, "check_workers"))
			num_check_workers = atoi(value);
		else if (!strcmp(variable, "query_socket")) {
			nm_free(qh_socket_path);
			qh_socket_path = nspath_absolute(value, config_file_dir);
		} else if (!strcmp(variable, "log_file")) {

			if (strlen(value) > MAX_FILENAME_LENGTH - 1) {
				nm_asprintf(&error_message, "Log file is too long");
				error = TRUE;
				break;
			}

			nm_free(log_file);
			log_file = nspath_absolute(value, config_file_dir);
			/* make sure the configured logfile takes effect */
			close_log_file();
		} else if (!strcmp(variable, "debug_level"))
			debug_level = atoi(value);

		else if (!strcmp(variable, "debug_verbosity"))
			debug_verbosity = atoi(value);

		else if (!strcmp(variable, "debug_file")) {

			if (strlen(value) > MAX_FILENAME_LENGTH - 1) {
				nm_asprintf(&error_message, "Debug log file is too long");
				error = TRUE;
				break;
			}

			nm_free(debug_file);
			debug_file = nspath_absolute(value, config_file_dir);
		}

		else if (!strcmp(variable, "max_debug_file_size"))
			max_debug_file_size = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "command_file")) {

			if (strlen(value) > MAX_FILENAME_LENGTH - 1) {
				nm_asprintf(&error_message, "Command file is too long");
				error = TRUE;
				break;
			}

			nm_free(command_file);
			command_file = nspath_absolute(value, config_file_dir);

			/* save the macro */
			mac->x[MACRO_COMMANDFILE] = command_file;
		}

		else if (!strcmp(variable, "temp_file")) {
			nm_free(temp_file);
			temp_file = nm_strdup(value);
		}

		else if (!strcmp(variable, "temp_path")) {
			nm_free(temp_path);
			temp_path = nspath_absolute(value, config_file_dir);
		}

		else if (!strcmp(variable, "check_result_path")) {
			if (strlen(value) > MAX_FILENAME_LENGTH - 1) {
				nm_asprintf(&error_message, "Check result path is too long");
				error = TRUE;
				break;
			}

			nm_free(check_result_path);
			check_result_path = nspath_absolute(value, config_file_dir);
			/* make sure we don't have a trailing slash */
			if (check_result_path[strlen(check_result_path) - 1] == '/')
				check_result_path[strlen(check_result_path) - 1] = '\x0';

			if ((tmpdir = opendir(check_result_path)) == NULL) {
				nm_asprintf(&error_message, "Warning: Failed to open check_result_path '%s': %s", check_result_path, strerror(errno));
				error = TRUE;
				break;
			}
			closedir(tmpdir);

		}

		else if (!strcmp(variable, "max_check_result_file_age")) {
			max_check_result_file_age = strtoul(value, NULL, 0);
		}

		else if (!strcmp(variable, "lock_file")) {

			if (strlen(value) > MAX_FILENAME_LENGTH - 1) {
				nm_asprintf(&error_message, "Lock file is too long");
				error = TRUE;
				break;
			}

			nm_free(lock_file);
			lock_file = nspath_absolute(value, config_file_dir);
		}

		else if (!strcmp(variable, "global_host_event_handler")) {
			nm_free(global_host_event_handler);
			global_host_event_handler = nm_strdup(value);
		}

		else if (!strcmp(variable, "global_service_event_handler")) {
			nm_free(global_service_event_handler);
			global_service_event_handler = nm_strdup(value);
		}

		else if (!strcmp(variable, "ocsp_command")) {
			nm_free(ocsp_command);
			ocsp_command = nm_strdup(value);
		}

		else if (!strcmp(variable, "ochp_command")) {
			nm_free(ochp_command);
			ochp_command = nm_strdup(value);
		}

		else if (!strcmp(variable, "nagios_user") ||
		         !strcmp(variable, "naemon_user") ||
		         !strcmp(variable, "nagios_group") ||
		         !strcmp(variable, "naemon_group")) {
			obsoleted_warning(variable, "Naemon is compiled to be run as " NAEMON_USER ":" NAEMON_GROUP);
		}

		else if (!strcmp(variable, "admin_email")) {

			/* save the macro */
			nm_free(mac->x[MACRO_ADMINEMAIL]);
			mac->x[MACRO_ADMINEMAIL] = nm_strdup(value);
		}

		else if (!strcmp(variable, "admin_pager")) {

			/* save the macro */
			nm_free(mac->x[MACRO_ADMINPAGER]);
			mac->x[MACRO_ADMINPAGER] = nm_strdup(value);
		}

		else if (!strcmp(variable, "use_syslog")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for use_syslog");
				error = TRUE;
				break;
			}

			use_syslog = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_notifications")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_notifications");
				error = TRUE;
				break;
			}

			log_notifications = (atoi(value) > 0) ? TRUE : FALSE;
		} else if (!strcmp(variable, "enable_notification_suppression_reason_logging")) {
			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for enable_notification_suppression_reason_logging");
				error = TRUE;
				break;
			}

			enable_notification_suppression_reason_logging = (atoi(value) > 0) ? TRUE : FALSE;

		} else if (!strcmp(variable, "log_service_retries")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_service_retries");
				error = TRUE;
				break;
			}

			log_service_retries = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_host_retries")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_host_retries");
				error = TRUE;
				break;
			}

			log_host_retries = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_event_handlers")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_event_handlers");
				error = TRUE;
				break;
			}

			log_event_handlers = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_external_commands")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_external_commands");
				error = TRUE;
				break;
			}

			log_external_commands = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_passive_checks")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_passive_checks");
				error = TRUE;
				break;
			}

			log_passive_checks = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_initial_states")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_initial_states");
				error = TRUE;
				break;
			}

			log_initial_states = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_current_states")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for log_current_states");
				error = TRUE;
				break;
			}

			log_current_states = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "retain_state_information")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for retain_state_information");
				error = TRUE;
				break;
			}

			retain_state_information = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "retention_update_interval")) {

			retention_update_interval = atoi(value);
			if (retention_update_interval < 0) {
				nm_asprintf(&error_message, "Illegal value for retention_update_interval");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "use_retained_program_state")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for use_retained_program_state");
				error = TRUE;
				break;
			}

			use_retained_program_state = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "use_retained_scheduling_info")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for use_retained_scheduling_info");
				error = TRUE;
				break;
			}

			use_retained_scheduling_info = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "retained_scheduling_randomize_window")) {

			retained_scheduling_randomize_window = atoi(value);
			if (retained_scheduling_randomize_window < 0) {
				nm_asprintf(&error_message, "Illegal value for retained_scheduling_randomize_window");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "retention_scheduling_horizon")) {

			retention_scheduling_horizon = atoi(value);

			if (retention_scheduling_horizon <= 0) {
				nm_asprintf(&error_message, "Illegal value for retention_scheduling_horizon");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "additional_freshness_latency"))
			additional_freshness_latency = atoi(value);

		else if (!strcmp(variable, "retained_host_attribute_mask"))
			retained_host_attribute_mask = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "retained_service_attribute_mask"))
			retained_service_attribute_mask = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "retained_process_host_attribute_mask"))
			retained_process_host_attribute_mask = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "retained_process_service_attribute_mask"))
			retained_process_service_attribute_mask = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "retained_contact_host_attribute_mask"))
			retained_contact_host_attribute_mask = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "retained_contact_service_attribute_mask"))
			retained_contact_service_attribute_mask = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "obsess_over_services")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for obsess_over_services");
				error = TRUE;
				break;
			}

			obsess_over_services = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "obsess_over_hosts")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for obsess_over_hosts");
				error = TRUE;
				break;
			}

			obsess_over_hosts = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "translate_passive_host_checks")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for translate_passive_host_checks");
				error = TRUE;
				break;
			}

			translate_passive_host_checks = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "passive_host_checks_are_soft"))
			passive_host_checks_are_soft = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "service_check_timeout")) {

			service_check_timeout = atoi(value);

			if (service_check_timeout <= 0) {
				nm_asprintf(&error_message, "Illegal value for service_check_timeout");
				error = TRUE;
				break;
			}
		}


		else if (!strcmp(variable, "service_check_timeout_state")) {

			if (!strcmp(value, "o"))
				service_check_timeout_state = STATE_OK;
			else if (!strcmp(value, "w"))
				service_check_timeout_state = STATE_WARNING;
			else if (!strcmp(value, "c"))
				service_check_timeout_state = STATE_CRITICAL;
			else if (!strcmp(value, "u"))
				service_check_timeout_state = STATE_UNKNOWN;
			else {
				nm_asprintf(&error_message, "Illegal value for service_check_timeout_state");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "host_check_timeout")) {

			host_check_timeout = atoi(value);

			if (host_check_timeout <= 0) {
				nm_asprintf(&error_message, "Illegal value for host_check_timeout");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "event_handler_timeout")) {

			event_handler_timeout = atoi(value);

			if (event_handler_timeout <= 0) {
				nm_asprintf(&error_message, "Illegal value for event_handler_timeout");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "notification_timeout")) {

			notification_timeout = atoi(value);

			if (notification_timeout <= 0) {
				nm_asprintf(&error_message, "Illegal value for notification_timeout");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "ocsp_timeout")) {

			ocsp_timeout = atoi(value);

			if (ocsp_timeout <= 0) {
				nm_asprintf(&error_message, "Illegal value for ocsp_timeout");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "ochp_timeout")) {

			ochp_timeout = atoi(value);

			if (ochp_timeout <= 0) {
				nm_asprintf(&error_message, "Illegal value for ochp_timeout");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "use_agressive_host_checking") || !strcmp(variable, "use_aggressive_host_checking")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for use_aggressive_host_checking");
				error = TRUE;
				break;
			}

			use_aggressive_host_checking = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "cached_host_check_horizon"))
			cached_host_check_horizon = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "enable_predictive_host_dependency_checks"))
			enable_predictive_host_dependency_checks = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "cached_service_check_horizon"))
			cached_service_check_horizon = strtoul(value, NULL, 0);

		else if (!strcmp(variable, "enable_predictive_service_dependency_checks"))
			enable_predictive_service_dependency_checks = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "soft_state_dependencies")) {
			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for soft_state_dependencies");
				error = TRUE;
				break;
			}

			soft_state_dependencies = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "log_rotation_method")) {
			obsoleted_warning(variable, "Logs are rotated by logrotate(8) - check your configuration");
		}

		else if (!strcmp(variable, "log_archive_path")) {
			/* FIXME: it's silly to have this here, despite naemon not using it.
			 * However, removing it removes the means for 3rd party addons to
			 * find the log archive at all.
			 */
			if (strlen(value) > MAX_FILENAME_LENGTH - 1) {
				nm_asprintf(&error_message, "Log archive path too long");
				error = TRUE;
				break;
			}

			nm_free(log_archive_path);
			log_archive_path = nspath_absolute(value, config_file_dir);
		}

		else if (!strcmp(variable, "enable_event_handlers"))
			enable_event_handlers = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "enable_notifications"))
			enable_notifications = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "execute_service_checks"))
			execute_service_checks = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "accept_passive_service_checks"))
			accept_passive_service_checks = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "execute_host_checks"))
			execute_host_checks = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "accept_passive_host_checks"))
			accept_passive_host_checks = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "service_inter_check_delay_method")) {
			obsoleted_warning(variable, "Service checks are delayed sanely - check your configuration");
		}

		else if (!strcmp(variable, "max_service_check_spread")) {
			obsoleted_warning(variable, "Service checks are delayed sanely - check your configuration");
		}

		else if (!strcmp(variable, "host_inter_check_delay_method")) {
			obsoleted_warning(variable, "Host checks are delayed sanely - check your configuration");
		}

		else if (!strcmp(variable, "max_host_check_spread")) {
			obsoleted_warning(variable, "Host checks are delayed sanely - check your configuration");
		}

		else if (!strcmp(variable, "service_interleave_factor")) {
			obsoleted_warning(variable, "Service checks are delayed sanely - check your configuration");
		}

		else if (!strcmp(variable, "max_concurrent_checks")) {

			max_parallel_service_checks = atoi(value);
			if (max_parallel_service_checks < 0) {
				nm_asprintf(&error_message, "Illegal value for max_concurrent_checks");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "check_result_reaper_frequency") || !strcmp(variable, "service_reaper_frequency")) {
			check_reaper_interval = atoi(value);
			if (check_reaper_interval < 1) {
				nm_asprintf(&error_message, "Illegal value for check_result_reaper_frequency");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "max_check_result_reaper_time")) {
			max_check_reaper_time = atoi(value);
			if (max_check_reaper_time < 1) {
				nm_asprintf(&error_message, "Illegal value for max_check_result_reaper_time");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "sleep_time")) {
			obsoleted_warning(variable, NULL);
		}

		else if (!strcmp(variable, "interval_length")) {

			interval_length = atoi(value);
			if (interval_length < 1) {
				nm_asprintf(&error_message, "Illegal value for interval_length");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "check_external_commands")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for check_external_commands");
				error = TRUE;
				break;
			}

			check_external_commands = (atoi(value) > 0) ? TRUE : FALSE;
		}

		/* @todo Remove before Nagios 4.3 */
		else if (!strcmp(variable, "command_check_interval")) {
			obsoleted_warning(variable, "Commands are always handled on arrival");
		}

		else if (!strcmp(variable, "check_for_orphaned_services")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for check_for_orphaned_services");
				error = TRUE;
				break;
			}

			check_orphaned_services = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "check_for_orphaned_hosts")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for check_for_orphaned_hosts");
				error = TRUE;
				break;
			}

			check_orphaned_hosts = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "check_service_freshness")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for check_service_freshness");
				error = TRUE;
				break;
			}

			check_service_freshness = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "check_host_freshness")) {

			if (strlen(value) != 1 || value[0] < '0' || value[0] > '1') {
				nm_asprintf(&error_message, "Illegal value for check_host_freshness");
				error = TRUE;
				break;
			}

			check_host_freshness = (atoi(value) > 0) ? TRUE : FALSE;
		}

		else if (!strcmp(variable, "service_freshness_check_interval") || !strcmp(variable, "freshness_check_interval")) {

			service_freshness_check_interval = atoi(value);
			if (service_freshness_check_interval <= 0) {
				nm_asprintf(&error_message, "Illegal value for service_freshness_check_interval");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "host_freshness_check_interval")) {

			host_freshness_check_interval = atoi(value);
			if (host_freshness_check_interval <= 0) {
				nm_asprintf(&error_message, "Illegal value for host_freshness_check_interval");
				error = TRUE;
				break;
			}
		} else if (!strcmp(variable, "auto_reschedule_checks")) {
			obsoleted_warning(variable, "Auto-rescheduling has been removed");
		}

		else if (!strcmp(variable, "auto_rescheduling_interval")) {
			obsoleted_warning(variable, "Auto-rescheduling has been removed");
		}

		else if (!strcmp(variable, "auto_rescheduling_window")) {
			obsoleted_warning(variable, "Auto-rescheduling has been removed");
		}

		else if (!strcmp(variable, "status_update_interval")) {
			status_update_interval = atoi(value);
		}

		else if (!strcmp(variable, "time_change_threshold")) {

			time_change_threshold = atoi(value);

			if (time_change_threshold <= 5) {
				nm_asprintf(&error_message, "Illegal value for time_change_threshold");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "process_performance_data"))
			process_performance_data = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "enable_flap_detection"))
			enable_flap_detection = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "enable_failure_prediction"))
			obsoleted_warning(variable, NULL);

		else if (!strcmp(variable, "low_service_flap_threshold")) {

			low_service_flap_threshold = strtod(value, NULL);
			if (low_service_flap_threshold <= 0.0 || low_service_flap_threshold >= 100.0) {
				nm_asprintf(&error_message, "Illegal value for low_service_flap_threshold");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "high_service_flap_threshold")) {

			high_service_flap_threshold = strtod(value, NULL);
			if (high_service_flap_threshold <= 0.0 ||  high_service_flap_threshold > 100.0) {
				nm_asprintf(&error_message, "Illegal value for high_service_flap_threshold");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "low_host_flap_threshold")) {

			low_host_flap_threshold = strtod(value, NULL);
			if (low_host_flap_threshold <= 0.0 || low_host_flap_threshold >= 100.0) {
				nm_asprintf(&error_message, "Illegal value for low_host_flap_threshold");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "high_host_flap_threshold")) {

			high_host_flap_threshold = strtod(value, NULL);
			if (high_host_flap_threshold <= 0.0 || high_host_flap_threshold > 100.0) {
				nm_asprintf(&error_message, "Illegal value for high_host_flap_threshold");
				error = TRUE;
				break;
			}
		}

		else if (!strcmp(variable, "date_format")) {

			if (!strcmp(value, "euro"))
				date_format = DATE_FORMAT_EURO;
			else if (!strcmp(value, "iso8601"))
				date_format = DATE_FORMAT_ISO8601;
			else if (!strcmp(value, "strict-iso8601"))
				date_format = DATE_FORMAT_STRICT_ISO8601;
			else
				date_format = DATE_FORMAT_US;
		}

		else if (!strcmp(variable, "use_timezone")) {
			nm_free(use_timezone);
			use_timezone = nm_strdup(value);
		}

		else if (!strcmp(variable, "event_broker_options")) {

			if (!strcmp(value, "-1"))
				event_broker_options = BROKER_EVERYTHING;
			else
				event_broker_options = strtoul(value, NULL, 0);
		}

		else if (!strcmp(variable, "illegal_object_name_chars"))
			illegal_object_chars = nm_strdup(value);

		else if (!strcmp(variable, "illegal_macro_output_chars"))
			illegal_output_chars = nm_strdup(value);


		else if (!strcmp(variable, "broker_module")) {
			modptr = strtok(value, " \n");
			argptr = strtok(NULL, "\n");
			modptr = nspath_absolute(modptr, config_file_dir);
			if (modptr) {
				neb_add_module(modptr, argptr, TRUE);
				free(modptr);
			} else {
				nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to allocate module path memory for '%s'\n", value);
			}
		}

		else if (!strcmp(variable, "use_regexp_matching"))
			use_regexp_matches = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "use_true_regexp_matching"))
			use_true_regexp_matching = (atoi(value) > 0) ? TRUE : FALSE;

		else if (!strcmp(variable, "daemon_dumps_core")) {
			obsoleted_warning(variable, "Use system facilities to control coredump behaviour instead");
		}

		/*** workers removed the need for these ***/
		else if (!strcmp(variable, "use_large_installation_tweaks"))
			obsoleted_warning(variable, "Naemon should always be fast");
		else if (!strcmp(variable, "enable_environment_macros"))
			obsoleted_warning(variable, NULL);
		else if (!strcmp(variable, "free_child_process_memory"))
			obsoleted_warning(variable, NULL);
		else if (!strcmp(variable, "child_processes_fork_twice"))
			obsoleted_warning(variable, NULL);

		/*** embedded perl variables are deprecated now ***/
		else if (!strcmp(variable, "enable_embedded_perl"))
			obsoleted_warning(variable, NULL);
		else if (!strcmp(variable, "use_embedded_perl_implicitly"))
			obsoleted_warning(variable, NULL);
		else if (!strcmp(variable, "auth_file"))
			obsoleted_warning(variable, NULL);
		else if (!strcmp(variable, "p1_file"))
			obsoleted_warning(variable, NULL);

		/*** as is external_command_buffer_slots */
		else if (!strcmp(variable, "external_command_buffer_slots"))
			obsoleted_warning(variable, "All commands are always processed upon arrival");

		else if (!strcmp(variable, "check_for_updates"))
			obsoleted_warning(variable, "Update checks allow spying and have been removed");
		else if (!strcmp(variable, "bare_update_check"))
			obsoleted_warning(variable, "Update checks allow spying and have been removed");

		/* BEGIN status data variables */
		else if (!strcmp(variable, "status_file"))
			status_file = nspath_absolute(value, config_file_dir);
		else if (strstr(input, "state_retention_file=") == input)
			retention_file = nspath_absolute(value, config_file_dir);
		/* END status data variables */

		/*** BEGIN perfdata variables ***/
		else if (!strcmp(variable, "perfdata_timeout")) {
			perfdata_timeout = atoi(value);
		} else if (!strcmp(variable, "host_perfdata_command"))
			host_perfdata_command = nm_strdup(value);
		else if (!strcmp(variable, "service_perfdata_command"))
			service_perfdata_command = nm_strdup(value);
		else if (!strcmp(variable, "host_perfdata_file_template"))
			host_perfdata_file_template = nm_strdup(value);
		else if (!strcmp(variable, "service_perfdata_file_template"))
			service_perfdata_file_template = nm_strdup(value);
		else if (!strcmp(variable, "host_perfdata_file"))
			host_perfdata_file = nspath_absolute(value, config_file_dir);
		else if (!strcmp(variable, "service_perfdata_file"))
			service_perfdata_file = nspath_absolute(value, config_file_dir);
		else if (!strcmp(variable, "host_perfdata_file_mode")) {
			host_perfdata_file_pipe = FALSE;
			if (strstr(value, "p") != NULL)
				host_perfdata_file_pipe = TRUE;
			else if (strstr(value, "w") != NULL)
				host_perfdata_file_append = FALSE;
			else
				host_perfdata_file_append = TRUE;
		} else if (!strcmp(variable, "service_perfdata_file_mode")) {
			service_perfdata_file_pipe = FALSE;
			if (strstr(value, "p") != NULL)
				service_perfdata_file_pipe = TRUE;
			else if (strstr(value, "w") != NULL)
				service_perfdata_file_append = FALSE;
			else
				service_perfdata_file_append = TRUE;
		} else if (!strcmp(variable, "host_perfdata_file_processing_interval"))
			host_perfdata_file_processing_interval = strtoul(value, NULL, 0);
		else if (!strcmp(variable, "service_perfdata_file_processing_interval"))
			service_perfdata_file_processing_interval = strtoul(value, NULL, 0);
		else if (!strcmp(variable, "host_perfdata_file_processing_command"))
			host_perfdata_file_processing_command = nm_strdup(value);
		else if (!strcmp(variable, "service_perfdata_file_processing_command"))
			service_perfdata_file_processing_command = nm_strdup(value);
		else if (!strcmp(variable, "host_perfdata_process_empty_results"))
			host_perfdata_process_empty_results = (atoi(value) > 0) ? TRUE : FALSE;
		else if (!strcmp(variable, "service_perfdata_process_empty_results"))
			service_perfdata_process_empty_results = (atoi(value) > 0) ? TRUE : FALSE;
		/*** END perfdata variables */

		else if (!strcmp(variable, "cfg_file")) {
			add_object_to_objectlist(&objcfg_files, nspath_absolute(value, config_file_dir));
		} else if (!strcmp(variable, "cfg_dir")) {
			add_object_to_objectlist(&objcfg_dirs, nspath_absolute(value, config_file_dir));
		} else if (!strcmp(variable, "include_file")) {
			char *include_file = nspath_absolute(value, config_file_dir);
			if (prepend_unique_object_to_objectlist(&maincfg_files, include_file, (int (*)(const void *, const void *))strcmp) == OBJECTLIST_DUPE) {
				error = TRUE;
				nm_asprintf(&error_message, "Error: File %s explicitly included more than once", include_file);
				break;
			}
			error |= read_config_file(include_file, mac);
			nm_free(include_file);
		} else if (!strcmp(variable, "include_dir")) {
			char *include_dir = nspath_absolute(value, config_file_dir);
			DIR *dirp = NULL;
			struct dirent *dirfile = NULL;

			if (prepend_unique_object_to_objectlist(&maincfg_dirs, include_dir, (int (*)(const void *, const void *))strcmp) == OBJECTLIST_DUPE) {
				error = TRUE;
				nm_asprintf(&error_message, "Error: Directory %s explicitly included more than once", include_dir);
				break;
			}
			dirp = opendir(include_dir);
			if (!dirp) {
				nm_asprintf(&error_message, "Error: Cannot open sub-configuration directory '%s' for reading!", include_dir);
				error = TRUE;
				break;
			} else {
				while ((dirfile = readdir(dirp)) != NULL) {
					int written_size;
					char file[MAX_FILENAME_LENGTH];

					/* skip hidden files and directories, current and parent dir, and non-config files */
					if (dirfile->d_name[0] == '.')
						continue;
					if (strcmp(dirfile->d_name + strlen(dirfile->d_name) - 4, ".cfg"))
						continue;

					/* create /path/to/file */
					written_size = snprintf(file, sizeof(file), "%s/%s", include_dir, dirfile->d_name);
					file[sizeof(file) - 1] = '\x0';

					/* Check for encoding errors */
					if (written_size < 0) {
						nm_log(NSLOG_RUNTIME_WARNING,
						       "Warning: encoding error on config file path '`%s'.\n", file);
						continue;
					}

					/* Check if the filename was truncated. */
					if (written_size > 0 && (size_t)written_size >= sizeof(file)) {
						nm_log(NSLOG_RUNTIME_WARNING,
						       "Warning: truncated path to config file '%s'.\n", file);
						continue;
					}

					error |= read_config_file(file, mac);
				}
				closedir(dirp);
			}
			nm_free(include_dir);
		} else if (strstr(input, "object_cache_file=") == input) {
			nm_free(object_cache_file);
			object_cache_file = nspath_absolute(value, config_file_dir);
			nm_free(mac->x[MACRO_OBJECTCACHEFILE]);
			mac->x[MACRO_OBJECTCACHEFILE] = nm_strdup(object_cache_file);
		} else if (strstr(input, "precached_object_file=") == input) {
			nm_free(object_precache_file);
			object_precache_file = nspath_absolute(value, config_file_dir);
		} else if (!strcmp(variable, "allow_empty_hostgroup_assignment")) {
			allow_empty_hostgroup_assignment = (atoi(value) > 0) ? TRUE : FALSE;
		} else if (!strcmp(variable, "allow_circular_dependencies")) {
			allow_circular_dependencies = atoi(value);
		} else if (!strcmp(variable, "host_down_disable_service_checks")) {
			host_down_disable_service_checks = strtoul(value, NULL, 0);
		} else if (!strcmp(variable, "service_parents_disable_service_checks")) {
			service_parents_disable_service_checks = strtoul(value, NULL, 0);
		} else if (!strcmp(variable, "service_skip_check_dependency_status")) {
			service_skip_check_dependency_status = atoi(value);
			if (service_skip_check_dependency_status < -2 || service_skip_check_dependency_status > 3) {
				nm_asprintf(&error_message, "Illegal value for service_skip_check_dependency_status");
				error = TRUE;
				break;
			}
		} else if (!strcmp(variable, "service_skip_check_host_down_status")) {
			service_skip_check_host_down_status = atoi(value);
			if (service_skip_check_host_down_status < -2 || service_skip_check_host_down_status > 3) {
				nm_asprintf(&error_message, "Illegal value for service_skip_check_host_down_status");
				error = TRUE;
				break;
			}
		} else if (!strcmp(variable, "host_skip_check_dependency_status")) {
			host_skip_check_dependency_status = atoi(value);
			if (host_skip_check_dependency_status < -2 || host_skip_check_dependency_status > 3) {
				nm_asprintf(&error_message, "Illegal value for host_skip_check_dependency_status");
				error = TRUE;
				break;
			}
		}
		/* skip external data directives */
		else if (strstr(input, "x") == input)
			continue;

		/* we don't know what this variable is... */
		else {
			nm_asprintf(&error_message, "UNKNOWN VARIABLE");
			error = TRUE;
			break;
		}
	}

	/* handle errors */
	if (error == TRUE) {
		nm_log(NSLOG_CONFIG_ERROR, "Error in configuration file '%s' - Line %d (%s)", main_config_file, current_line, (error_message == NULL) ? "NULL" : error_message);
		return ERROR;
	}

	if (deprecated) {
		objectlist *list;
		for (list = deprecated; list; list = list->next) {
			nm_log(NSLOG_CONFIG_WARNING, "%s", (char *)list->object_ptr);
			free(list->object_ptr);
		}
		free_objectlist(&deprecated);
		deprecated = NULL;
	}

	mmap_fclose(thefile);
	nm_free(input);
	nm_free(variable);
	nm_free(value);
	nm_free(error_message);

	return OK;
}

/* process the main configuration file */
int read_main_config_file(const char *main_config_file)
{
	DIR *tmpdir = NULL;
	nagios_macros *mac;

	mac = get_global_macros();

	/* save the main config file macro */
	nm_free(mac->x[MACRO_MAINCONFIGFILE]);
	if ((mac->x[MACRO_MAINCONFIGFILE] = nm_strdup(main_config_file)))
		strip(mac->x[MACRO_MAINCONFIGFILE]);

	if (read_config_file(main_config_file, mac) != OK)
		return ERROR;

	free_objectlist(&maincfg_files);
	free_objectlist(&maincfg_dirs);

	if (!temp_path) {
		temp_path = getenv("TMPDIR");
		if (!temp_path)
			temp_path = getenv("TMP");
		if (!temp_path)
			temp_path = "/tmp";

		temp_path = nm_strdup(temp_path);
	} else {
		/* make sure we don't have a trailing slash */
		if (temp_path[strlen(temp_path) - 1] == '/')
			temp_path[strlen(temp_path) - 1] = '\x0';
	}

	if ((strlen(temp_path) > MAX_FILENAME_LENGTH - 1)) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: temp_path is too long\n");
		return ERROR;
	}
	if ((tmpdir = opendir(temp_path)) == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: temp_path '%s' is not a valid directory\n", temp_path);
		return ERROR;
	}
	closedir(tmpdir);

	/* now that we know we have temp_path, we can set temp_file properly */
	if (!temp_file) {
		temp_file = nspath_absolute("nagios.tmp", temp_path);
	} else if (*temp_file == '.') {
		/* temp_file is relative. Make it naemon.cfg-relative */
		char *foo = temp_file;
		temp_file = nspath_absolute(temp_file, config_file_dir);
		free(foo);
	} else if (*temp_file != '/') {
		/*
		 * tempfile is not relative and not absolute, so
		 * put it in temp_path
		 */
		char *foo = temp_file;
		temp_file = nspath_absolute(temp_file, temp_path);
		free(foo);
	}

	if (strlen(temp_file) > MAX_FILENAME_LENGTH - 1) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Temp file '%s' is too long\n", temp_file);
		return ERROR;
	}

	/* save the macros */
	mac->x[MACRO_TEMPPATH] = temp_path;
	mac->x[MACRO_TEMPFILE] = temp_file;

	/* adjust timezone values */
	if (use_timezone != NULL)
		set_environment_var("TZ", use_timezone, 1);
	tzset();

	/* make sure a log file has been specified */
	strip(log_file);
	if (!log_file || !strcmp(log_file, "")) {
		if (daemon_mode == FALSE)
			printf("Error: Log file is not specified anywhere in main config file '%s'!\n", main_config_file);
		return ERROR;
	}

	return OK;
}


/* processes macros in resource file */
int read_resource_file(const char *resource_file)
{
	char *input = NULL;
	char *variable = NULL;
	char *value = NULL;
	char *temp_ptr = NULL;
	mmapfile *thefile = NULL;
	int current_line = 1;
	int error = FALSE;
	int user_index = 0;

	if ((thefile = mmap_fopen(resource_file)) == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Cannot open resource file '%s' for reading!", resource_file);
		return ERROR;
	}

	/* process all lines in the resource file */
	while (1) {

		nm_free(input);
		nm_free(variable);
		nm_free(value);

		/* read the next line */
		if ((input = mmap_fgets_multiline(thefile)) == NULL)
			break;

		current_line = thefile->current_line;

		/* skip blank lines and comments */
		if (input[0] == '#' || input[0] == '\x0' || input[0] == '\n' || input[0] == '\r')
			continue;

		strip(input);

		/* get the variable name */
		if ((temp_ptr = my_strtok(input, "=")) == NULL) {
			nm_log(NSLOG_CONFIG_ERROR, "Error: NULL variable - Line %d of resource file '%s'", current_line, resource_file);
			error = TRUE;
			break;
		}
		variable = (char *)nm_strdup(temp_ptr);

		/* get the value */
		if ((temp_ptr = my_strtok(NULL, "\n")) == NULL) {
			nm_log(NSLOG_CONFIG_ERROR, "Error: NULL variable value - Line %d of resource file '%s'", current_line, resource_file);
			error = TRUE;
			break;
		}
		value = nm_strdup(temp_ptr);
		/* what should we do with the variable/value pair? */

		/* check for macro declarations */
		if (variable[0] == '$' && variable[strlen(variable) - 1] == '$') {

			/* $USERx$ macro declarations */
			if (strstr(variable, "$USER") == variable  && strlen(variable) > 5) {
				user_index = atoi(variable + 5) - 1;
				if (user_index >= 0 && user_index < MAX_USER_MACROS) {
					nm_free(macro_user[user_index]);
					macro_user[user_index] = nm_strdup(value);
				}
			}
		}
	}

	/* free leftover memory and close the file */
	nm_free(input);
	mmap_fclose(thefile);

	nm_free(variable);
	nm_free(value);

	if (error == TRUE)
		return ERROR;

	return OK;
}


/****************************************************************/
/**************** CONFIG VERIFICATION FUNCTIONS *****************/
/****************************************************************/

/* do a pre-flight check to make sure object relationships, etc. make sense */
int pre_flight_check(void)
{
	char *buf = NULL;
	int warnings = 0;
	int errors = 0;
	int temp_path_fd = -1;


	/********************************************/
	/* check object relationships               */
	/********************************************/
	pre_flight_object_check(&warnings, &errors);

	/********************************************/
	/* check for circular paths between hosts   */
	/********************************************/
	if (!allow_circular_dependencies) {
		pre_flight_circular_check(&warnings, &errors);
	}

	/********************************************/
	/* check global event handler commands...   */
	/********************************************/
	if (verify_config)
		printf("Checking global event handlers...\n");
	if (global_host_event_handler != NULL) {
		global_host_event_handler_ptr = find_bang_command(global_host_event_handler);
		if (global_host_event_handler_ptr == NULL) {
			nm_log(NSLOG_VERIFICATION_ERROR, "Error: Global host event handler command '%s' is not defined anywhere!", global_host_event_handler);
			errors++;
		}
	}
	if (global_service_event_handler != NULL) {
		global_service_event_handler_ptr = find_bang_command(global_service_event_handler);
		if (global_service_event_handler_ptr == NULL) {
			nm_log(NSLOG_VERIFICATION_ERROR, "Error: Global service event handler command '%s' is not defined anywhere!", global_service_event_handler);
			errors++;
		}
	}


	/**************************************************/
	/* check obsessive processor commands...          */
	/**************************************************/
	if (verify_config)
		printf("Checking obsessive compulsive processor commands...\n");
	if (ocsp_command != NULL) {
		ocsp_command_ptr = find_bang_command(ocsp_command);
		if (!ocsp_command_ptr) {
			nm_log(NSLOG_CONFIG_ERROR, "Error: OCSP command '%s' is not defined anywhere!\n", ocsp_command);
			errors++;
		}
	}
	if (ochp_command != NULL) {
		ochp_command_ptr = find_bang_command(ochp_command);
		if (!ochp_command_ptr) {
			nm_log(NSLOG_CONFIG_ERROR, "Error: OCHP command '%s' is not defined anywhere!\n", ochp_command);
			errors++;
		}
	}


	/**************************************************/
	/* check various settings...                      */
	/**************************************************/
	if (verify_config)
		printf("Checking misc settings...\n");

	/* check if we can write to temp_path */
	nm_asprintf(&buf, "%s/nagiosXXXXXX", temp_path);
	if ((temp_path_fd = mkstemp(buf)) == -1) {
		nm_log(NSLOG_VERIFICATION_ERROR, "\tError: Unable to write to temp_path ('%s') - %s\n", temp_path, strerror(errno));
		errors++;
	} else {
		close(temp_path_fd);
		remove(buf);
	}
	nm_free(buf);

	/* check if we can write to check_result_path */
	nm_asprintf(&buf, "%s/nagiosXXXXXX", check_result_path);
	if ((temp_path_fd = mkstemp(buf)) == -1) {
		nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Unable to write to check_result_path ('%s') - %s\n", check_result_path, strerror(errno));
		warnings++;
	} else {
		close(temp_path_fd);
		remove(buf);
	}
	nm_free(buf);

	/* warn if user didn't specify any illegal macro output chars */
	if (illegal_output_chars == NULL) {
		nm_log(NSLOG_VERIFICATION_WARNING, "%s", "Warning: Nothing specified for illegal_macro_output_chars variable!\n");
		warnings++;
	} else {
		char *p;
		for (p = illegal_output_chars; *p; p++) {
			illegal_output_char_map[(int)*p] = 1;
		}
	}

	if (verify_config) {
		printf("\n");
		printf("Total Warnings: %d\n", warnings);
		printf("Total Errors:   %d\n", errors);
	}

	return (errors > 0) ? ERROR : OK;
}


/* do a pre-flight check to make sure object relationships make sense */
int pre_flight_object_check(int *w, int *e)
{
	contact *temp_contact = NULL;
	host *temp_host = NULL;
	service *temp_service = NULL;
	int total_objects = 0;
	int warnings = 0;
	int errors = 0;


#ifdef TEST
	void *ptr = NULL;
	char *buf1 = "";
	char *buf2 = "";
	buf1 = "temptraxe1";
	buf2 = "Probe 2";
	for (temp_se = get_first_serviceescalation_by_service(buf1, buf2, &ptr); temp_se != NULL; temp_se = get_next_serviceescalation_by_service(buf1, buf2, &ptr)) {
		printf("FOUND ESCALATION FOR SVC '%s'/'%s': %d-%d/%.3f, PTR=%p\n", buf1, buf2, temp_se->first_notification, temp_se->last_notification, temp_se->notification_interval, ptr);
	}
	for (temp_he = get_first_hostescalation_by_host(buf1, &ptr); temp_he != NULL; temp_he = get_next_hostescalation_by_host(buf1, &ptr)) {
		printf("FOUND ESCALATION FOR HOST '%s': %d-%d/%d, PTR=%p\n", buf1, temp_he->first_notification, temp_he->last_notification, temp_he->notification_interval, ptr);
	}
#endif

	if (verify_config)
		printf("Checking objects...\n");

	/*****************************************/
	/* check each service...                 */
	/*****************************************/
	total_objects = 0;
	for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

		total_objects++;

		/* check for sane recovery options */
		if (temp_service->notification_options == OPT_RECOVERY) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Recovery notification option in service '%s' for host '%s' doesn't make any sense - specify warning and/or critical options as well", temp_service->description, temp_service->host_name);
			warnings++;
		}

		/* check to see if there is at least one contact/group */
		if (temp_service->contacts == NULL && temp_service->contact_groups == NULL) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Service '%s' on host '%s' has no default contacts or contactgroups defined!", temp_service->description, temp_service->host_name);
			warnings++;
		}

		/* verify service check timeperiod */
		if (temp_service->check_period == NULL) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Service '%s' on host '%s' has no check time period defined!", temp_service->description, temp_service->host_name);
			warnings++;
		}

		/* check service notification timeperiod */
		if (temp_service->notification_period == NULL) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Service '%s' on host '%s' has no notification time period defined!", temp_service->description, temp_service->host_name);
			warnings++;
		}

		/* see if the notification interval is less than the check interval */
		if (temp_service->notification_interval < temp_service->check_interval && temp_service->notification_interval != 0) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Service '%s' on host '%s'  has a notification interval less than its check interval!  Notifications are only re-sent after checks are made, so the effective notification interval will be that of the check interval.", temp_service->description, temp_service->host_name);
			warnings++;
		}
	}

	if (verify_config)
		printf("\tChecked %d services.\n", total_objects);



	/*****************************************/
	/* check all hosts...                    */
	/*****************************************/
	total_objects = 0;
	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

		total_objects++;

		/* make sure each host has at least one service associated with it */
		if (temp_host->services == NULL && verify_config >= 2) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Host '%s' has no services associated with it!", temp_host->name);
			warnings++;
		}

		/* check to see if there is at least one contact/group */
		if (temp_host->contacts == NULL && temp_host->contact_groups == NULL) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Host '%s' has no default contacts or contactgroups defined!", temp_host->name);
			warnings++;
		}
		/* check for sane recovery options */
		if (temp_host->notification_options == OPT_RECOVERY) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Recovery notification option in host '%s' definition doesn't make any sense - specify down and/or unreachable options as well", temp_host->name);
			warnings++;
		}
	}

	if (verify_config)
		printf("\tChecked %d hosts.\n", total_objects);


	/*****************************************/
	/* check all contacts...                 */
	/*****************************************/
	for (temp_contact = contact_list, total_objects = 0; temp_contact != NULL; temp_contact = temp_contact->next, total_objects++) {

		/* check service notification commands */
		if (temp_contact->service_notification_commands == NULL) {
			nm_log(NSLOG_VERIFICATION_ERROR, "Error: Contact '%s' has no service notification commands defined!", temp_contact->name);
			errors++;
		}

		/* check host notification commands */
		if (temp_contact->host_notification_commands == NULL) {
			nm_log(NSLOG_VERIFICATION_ERROR, "Error: Contact '%s' has no host notification commands defined!", temp_contact->name);
			errors++;
		}

		/* check service notification timeperiod */
		if (temp_contact->service_notification_period == NULL) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Contact '%s' has no service notification time period defined!", temp_contact->name);
			warnings++;
		}

		/* check host notification timeperiod */
		if (temp_contact->host_notification_period == NULL) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Contact '%s' has no host notification time period defined!", temp_contact->name);
			warnings++;
		}

		/* check for sane host recovery options */
		if (temp_contact->host_notification_options == OPT_RECOVERY) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Host recovery notification option for contact '%s' doesn't make any sense - specify down and/or unreachable options as well", temp_contact->name);
			warnings++;
		}

		/* check for sane service recovery options */
		if (temp_contact->service_notification_options == OPT_RECOVERY) {
			nm_log(NSLOG_VERIFICATION_WARNING, "Warning: Service recovery notification option for contact '%s' doesn't make any sense - specify critical and/or warning options as well", temp_contact->name);
			warnings++;
		}
	}

	if (verify_config)
		printf("\tChecked %d contacts.\n", total_objects);

	/* help people use scripts to verify that objects are loaded */
	if (verify_config) {
		printf("\tChecked %u host groups.\n", num_objects.hostgroups);
		printf("\tChecked %d service groups.\n", num_objects.servicegroups);
		printf("\tChecked %d contact groups.\n", num_objects.contactgroups);
		printf("\tChecked %d commands.\n", num_objects.commands);
		printf("\tChecked %d time periods.\n", num_objects.timeperiods);
		printf("\tChecked %u host escalations.\n", num_objects.hostescalations);
		printf("\tChecked %u service escalations.\n", num_objects.serviceescalations);
	}

	/* update warning and error count */
	*w += warnings;
	*e += errors;

	return (errors > 0) ? ERROR : OK;
}


/* dfs status values */
#define DFS_UNCHECKED                    0  /* default value */
#define DFS_TEMP_CHECKED                 1  /* check just one time */
#define DFS_OK                           2  /* no problem */

/**
 * Modified version of Depth-first Search
 * http://en.wikipedia.org/wiki/Depth-first_search
 *
 * In a dependency tree like this (parent->child, dep->dep or whatever):
 * A - B   C
 *      \ /
 *       D
 *      / \
 * E - F - G
 *   /  \\
 * H     H
 *
 * ... we look at the nodes in the following order:
 * A B D C G (marking all of them as OK)
 * E F D G H F (D,G are already OK, E is marked near-loopy F and H are loopy)
 * H (which is already marked as loopy, so we don't follow it)
 *
 * We look at each node at most once per parent, so the algorithm has
 * O(nx) worst-case complexity,, where x is the average number of
 * parents.
 */
/*
 * same as dfs_host_path, but we flip the tree and traverse it
 * backwards, since core Nagios doesn't need the child pointer at
 * later stages.
 */

struct dfs_parameters {
	char *ary;
	int *errors;
};

static gboolean dfs_host_path_cb(gpointer _name, gpointer _hst, gpointer user_data);
static int dfs_host_path(host *root, struct dfs_parameters *params);

static int dfs_servicedep_path(char *ary, servicedependency *root)
{
	objectlist *olist;

	if (!root)
		return 0;
	if (ary[root->id] == DFS_TEMP_CHECKED) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Circular %s dependency detected between service '%s;%s' and '%s;%s'\n",
		       root->dependency_type == NOTIFICATION_DEPENDENCY ? "notification" : "execution",
		       root->dependent_host_name, root->dependent_service_description,
		       root->master_service_ptr->host_name, root->master_service_ptr->description);
		return 1;
	} else if (ary[root->id] != DFS_UNCHECKED)
		return ary[root->id] != DFS_OK;


	ary[root->id] = DFS_TEMP_CHECKED;

	if (root->dependency_type == NOTIFICATION_DEPENDENCY) {
		for (olist = root->master_service_ptr->notify_deps; olist; olist = olist->next) {
			int ret = dfs_servicedep_path(ary, olist->object_ptr);
			if (ret)
				return ret;
		}
	} else {
		for (olist = root->master_service_ptr->exec_deps; olist; olist = olist->next) {
			int ret = dfs_servicedep_path(ary, olist->object_ptr);
			if (ret)
				return ret;
		}
	}

	/*
	 * if we've hit ourself, we'll have marked us as loopy
	 * above, so if we're TEMP_CHECKED still we're ok
	 */
	if (ary[root->id] == DFS_TEMP_CHECKED)
		ary[root->id] = DFS_OK;
	return ary[root->id] != DFS_OK;
}


static int dfs_hostdep_path(char *ary, hostdependency *root)
{
	objectlist *olist;

	if (!root)
		return 0;

	if (ary[root->id] == DFS_TEMP_CHECKED) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Circular %s dependency detected between host '%s' and '%s'\n",
		       root->dependency_type == NOTIFICATION_DEPENDENCY ? "notification" : "execution",
		       root->dependent_host_name,
		       root->master_host_ptr->name);
		return 1;
	} else if (ary[root->id] != DFS_UNCHECKED)
		return ary[root->id] != DFS_OK;

	ary[root->id] = DFS_TEMP_CHECKED;

	if (root->dependency_type == NOTIFICATION_DEPENDENCY) {
		for (olist = root->master_host_ptr->notify_deps; olist; olist = olist->next) {
			int ret = dfs_hostdep_path(ary, olist->object_ptr);
			if (ret)
				return ret;
		}
	} else {
		for (olist = root->master_host_ptr->exec_deps; olist; olist = olist->next) {
			int ret = dfs_hostdep_path(ary, olist->object_ptr);
			if (ret)
				return ret;
		}
	}

	/*
	 * if we've hit ourself, we'll have marked us as loopy
	 * above, so if we're still TEMP_CHECKED we're ok
	 */
	if (ary[root->id] == DFS_TEMP_CHECKED)
		ary[root->id] = DFS_OK;
	return ary[root->id] != DFS_OK;
}

static gboolean dfs_host_path_cb(gpointer _name, gpointer _hst, gpointer user_data)
{
	return 0 != dfs_host_path((host *)_hst, (struct dfs_parameters *)user_data);
}

static int dfs_host_path(host *root, struct dfs_parameters *params)
{
	char *ary = params->ary;
	int *errors = params->errors;
	if (!root)
		return 0;

	if (ary[root->id] == DFS_TEMP_CHECKED) {
		nm_log(
		    NSLOG_VERIFICATION_ERROR,
		    "Error: The host '%s' is part of a circular parent/child chain!",
		    root->name);
		(*errors)++;
		return 0;
	} else if (ary[root->id] != DFS_UNCHECKED) {
		if (ary[root->id] != DFS_OK)
			(*errors)++;
		return 0;
	}

	/* Mark the root temporary checked */
	ary[root->id] = DFS_TEMP_CHECKED;

	g_tree_foreach(root->child_hosts, dfs_host_path_cb, params);

	if (ary[root->id] == DFS_TEMP_CHECKED)
		ary[root->id] = DFS_OK;

	if (ary[root->id] != DFS_OK)
		(*errors)++;
	return 0;
}


static int dfs_timeperiod_path(char *ary, timeperiod *root)
{
	timeperiodexclusion *exc;

	if (!root)
		return 0;

	if (ary[root->id] == DFS_TEMP_CHECKED) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: The timeperiod '%s' is part of a circular exclusion chain!",
		       root->name);
		return 1;
	} else if (ary[root->id] != DFS_UNCHECKED)
		return ary[root->id] != DFS_OK;

	/* Mark the root temporary checked */
	ary[root->id] = DFS_TEMP_CHECKED;

	for (exc = root->exclusions; exc; exc = exc->next) {
		int ret = dfs_timeperiod_path(ary, exc->timeperiod_ptr);
		if (ret)
			return ret;
	}

	if (ary[root->id] == DFS_TEMP_CHECKED)
		ary[root->id] = DFS_OK;
	return ary[root->id] != DFS_OK;
}


/* check for circular paths and dependencies */
int pre_flight_circular_check(int *w, int *e)
{
	host *temp_host = NULL;
	servicedependency *temp_sd = NULL;
	hostdependency *temp_hd = NULL;
	timeperiod *tp;
	unsigned int i;
	int errors = 0;
	unsigned int alloc;
	char *ary;
	struct dfs_parameters params;

	if (num_objects.hosts > num_objects.services)
		alloc = num_objects.hosts;
	else
		alloc = num_objects.services;
	if (num_objects.timeperiods > alloc)
		alloc = num_objects.timeperiods;
	if (num_objects.hostdependencies > alloc)
		alloc = num_objects.hostdependencies;
	if (num_objects.servicedependencies > alloc)
		alloc = num_objects.servicedependencies;

	ary = nm_calloc(1, alloc);


	/********************************************/
	/* check for circular paths between hosts   */
	/********************************************/
	if (verify_config)
		printf("Checking for circular paths...\n");

	params.ary = ary;
	params.errors = &errors;
	for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {
		dfs_host_path(temp_host, &params);
	}
	if (verify_config)
		printf("\tChecked %u hosts\n", num_objects.hosts);

	/********************************************/
	/* check for circular dependencies          */
	/********************************************/
	/* check service dependencies */
	/* We must clean the dfs status from previous check */
	memset(ary, 0, alloc);
	for (i = 0; i < num_objects.services; i++) {
		struct objectlist *deplist;
		for (deplist = service_ary[i]->notify_deps; deplist; deplist = deplist->next) {
			temp_sd = deplist->object_ptr;
			errors += dfs_servicedep_path(ary, temp_sd);
		}
		for (deplist = service_ary[i]->exec_deps; deplist; deplist = deplist->next) {
			temp_sd = deplist->object_ptr;
			errors += dfs_servicedep_path(ary, temp_sd);
		}
	}
	if (verify_config)
		printf("\tChecked %u service dependencies\n", num_objects.servicedependencies);

	/* check host dependencies */
	memset(ary, 0, alloc);
	for (i = 0; i < num_objects.hosts; i++) {
		struct objectlist *deplist;
		for (deplist = host_ary[i]->notify_deps; deplist; deplist = deplist->next) {
			temp_hd = deplist->object_ptr;
			errors += dfs_hostdep_path(ary, temp_hd);
		}
		for (deplist = host_ary[i]->exec_deps; deplist; deplist = deplist->next) {
			temp_hd = deplist->object_ptr;
			errors += dfs_hostdep_path(ary, temp_hd);
		}
	}

	if (verify_config)
		printf("\tChecked %u host dependencies\n", num_objects.hostdependencies);

	/* check timeperiod exclusion chains */
	memset(ary, 0, alloc);
	for (tp = timeperiod_list; tp; tp = tp->next) {
		errors += dfs_timeperiod_path(ary, tp);
	}
	if (verify_config)
		printf("\tChecked %u timeperiods\n", num_objects.timeperiods);

	/* update warning and error count */
	*e += errors;

	free(ary);

	return (errors > 0) ? ERROR : OK;
}
