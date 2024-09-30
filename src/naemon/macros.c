#include "config.h"
#include "macros.h"
#include "common.h"
#include "statusdata.h"
#include "comments.h"
#include "utils.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"
#include "broker.h"
#include <string.h>
#include <glib.h>

static char *macro_x_names[MACRO_X_COUNT]; /* the macro names */
char *macro_user[MAX_USER_MACROS]; /* $USERx$ macros */

struct macro_key_code {
	char *name;  /* macro key name */
	char *value; /* macro value */
	int code;  /* numeric macro code, usable in case statements */
	int options; /* Options for how the macro can be escaped */
};

static struct macro_key_code macro_keys[MACRO_X_COUNT];

/*
 * These point to their corresponding pointer arrays in global_macros
 * AFTER macros have been initialized.
 *
 * They really only exist so that eventbroker modules that reference
 * them won't need to be re-compiled, although modules that rely
 * on their values after having run a certain command will require an
 * update
 */
static char **macro_x = NULL;

/*
 * scoped to this file to prevent (unintentional) mischief,
 * but see base/notifications.c for how to use it
 */
static nagios_macros global_macros;

struct grab_macro_value_parameters {
	nagios_macros *mac;
	int macro_type;
	GString *buffer;
	char *delimiter;
};

struct grab_custom_value_parameters {
	nagios_macros *mac;
	char *macro_name;
	GString *buffer;
	char *delimiter;
};


/* prototypes for recursive or chain-recursive functions */
static int grab_custom_macro_value_r(nagios_macros *mac, char *macro_name, char *arg1, char *arg2, char **output);


nagios_macros *get_global_macros(void)
{
	return &global_macros;
}

/*
 * locate a macro key based on its name by using a binary search
 * over all keys. O(log(n)) complexity and a vast improvement over
 * the previous linear scan
 */
static const struct macro_key_code *find_macro_key(const char *name)
{
	unsigned int high, low = 0;
	int value;
	struct macro_key_code *key;

	high = MACRO_X_COUNT;
	while (high > low) {
		unsigned int mid = low + ((high - low) / 2);
		key = &macro_keys[mid];
		value = strcmp(name, key->name);
		if (value == 0) {
			return key;
		}
		if (value > 0)
			low = mid + 1;
		else
			high = mid;
	}
	return NULL;
}


/* computes a custom object macro */
static int grab_custom_object_macro_r(nagios_macros *mac, char *macro_name, customvariablesmember *vars, char **output)
{
	customvariablesmember *temp_customvariablesmember = NULL;
	int result = ERROR;

	if (macro_name == NULL || vars == NULL || output == NULL)
		return ERROR;

	/* get the custom variable */
	for (temp_customvariablesmember = vars; temp_customvariablesmember != NULL; temp_customvariablesmember = temp_customvariablesmember->next) {

		if (temp_customvariablesmember->variable_name == NULL)
			continue;

		if (!strcmp(macro_name, temp_customvariablesmember->variable_name)) {
			if (temp_customvariablesmember->variable_value)
				*output = temp_customvariablesmember->variable_value;
			result = OK;
			break;
		}
	}

	return result;
}

/* given a "raw" command, return the "expanded" or "whole" command line */
int get_raw_command_line_r(nagios_macros *mac, command *cmd_ptr, char *cmd, char **full_command, int macro_options)
{
	char *temp_arg = NULL;
	char *arg_buffer = NULL;
	size_t cmd_len = 0;
	register int x = 0;
	register int y = 0;
	register int arg_index = 0;

	/* clear the argv macros */
	clear_argv_macros_r(mac);

	/* make sure we've got all the requirements */
	if (cmd_ptr == NULL || full_command == NULL)
		return ERROR;

	log_debug_info(DEBUGL_COMMANDS | DEBUGL_CHECKS | DEBUGL_MACROS, 2, "Raw Command Input: %s\n", cmd_ptr->command_line);

	/* get the full command line */
	*full_command = nm_strdup((cmd_ptr->command_line == NULL) ? "" : cmd_ptr->command_line);

	if (cmd == NULL) {
		log_debug_info(DEBUGL_COMMANDS | DEBUGL_CHECKS | DEBUGL_MACROS, 2, "Expanded Command Output: %s\n", *full_command);
		return OK;
	}

	cmd_len = strlen(cmd);
	temp_arg = nm_malloc(cmd_len);

	/* get the command arguments */
	/* skip the command name (we're about to get the arguments)... */
	for (arg_index = 0;; arg_index++) {
		if (cmd[arg_index] == '!' || cmd[arg_index] == '\x0')
			break;
	}

	/* get each command argument */
	for (x = 0; x < MAX_COMMAND_ARGUMENTS; x++) {

		/* we reached the end of the arguments... */
		if (cmd[arg_index] == '\x0')
			break;

		/* get the next argument */
		/* can't use strtok(), as that's used in process_macros... */
		for (arg_index++, y = 0; y < (int)cmd_len - 1; arg_index++) {

			/* handle escaped argument delimiters */
			if (cmd[arg_index] == '\\' && cmd[arg_index + 1] == '!') {
				arg_index++;
			} else if (cmd[arg_index] == '!' || cmd[arg_index] == '\x0') {
				/* end of argument */
				break;
			}

			/* copy the character */
			temp_arg[y] = cmd[arg_index];
			y++;
		}
		temp_arg[y] = '\x0';

		/* ADDED 01/29/04 EG */
		/* process any macros we find in the argument */
		process_macros_r(mac, temp_arg, &arg_buffer, macro_options);

		mac->argv[x] = arg_buffer;
	}

	log_debug_info(DEBUGL_COMMANDS | DEBUGL_CHECKS | DEBUGL_MACROS, 2, "Expanded Command Output: %s\n", *full_command);

	nm_free(temp_arg);
	return OK;
}

/* grab macros that are specific to a particular host */
int grab_host_macros_r(nagios_macros *mac, host *hst)
{
	/* clear host-related macros */
	clear_host_macros_r(mac);
	clear_hostgroup_macros_r(mac);

	/* save pointer to host */
	mac->host_ptr = hst;
	mac->hostgroup_ptr = NULL;

	if (hst == NULL)
		return ERROR;

	/* save pointer to host's first/primary hostgroup */
	if (hst->hostgroups_ptr)
		mac->hostgroup_ptr = (hostgroup *)hst->hostgroups_ptr->object_ptr;

	return OK;
}

int grab_host_macros(host *hst)
{
	return grab_host_macros_r(&global_macros, hst);
}


/* grab hostgroup macros */
int grab_hostgroup_macros_r(nagios_macros *mac, hostgroup *hg)
{
	/* clear hostgroup macros */
	clear_hostgroup_macros_r(mac);

	/* save the hostgroup pointer for later */
	mac->hostgroup_ptr = hg;

	if (hg == NULL)
		return ERROR;

	return OK;
}

int grab_hostgroup_macros(hostgroup *hg)
{
	return grab_hostgroup_macros_r(&global_macros, hg);
}


/* grab macros that are specific to a particular service and its associated host */
int grab_service_macros_r(nagios_macros *mac, service *svc)
{
	/* first grab the macros for this service's associated host */
	grab_host_macros_r(mac, svc->host_ptr);
	/* clear service-related macros */
	clear_service_macros_r(mac);
	clear_servicegroup_macros_r(mac);

	/* save pointer for later */
	mac->service_ptr = svc;
	mac->servicegroup_ptr = NULL;

	if (svc == NULL)
		return ERROR;

	/* save first/primary servicegroup pointer for later */
	if (svc->servicegroups_ptr)
		mac->servicegroup_ptr = (servicegroup *)svc->servicegroups_ptr->object_ptr;

	return OK;
}

int grab_service_macros(service *svc)
{
	return grab_service_macros_r(&global_macros, svc);
}


/* grab macros that are specific to a particular servicegroup */
int grab_servicegroup_macros_r(nagios_macros *mac, servicegroup *sg)
{
	/* clear servicegroup macros */
	clear_servicegroup_macros_r(mac);

	/* save the pointer for later */
	mac->servicegroup_ptr = sg;

	if (sg == NULL)
		return ERROR;

	return OK;
}

int grab_servicegroup_macros(servicegroup *sg)
{
	return grab_servicegroup_macros_r(&global_macros, sg);
}


/* grab macros that are specific to a particular contact */
int grab_contact_macros_r(nagios_macros *mac, contact *cntct)
{
	/* clear contact-related macros */
	clear_contact_macros_r(mac);
	clear_contactgroup_macros_r(mac);

	/* save pointer to contact for later */
	mac->contact_ptr = cntct;
	mac->contactgroup_ptr = NULL;

	if (cntct == NULL)
		return ERROR;

	/* save pointer to first/primary contactgroup for later */
	if (cntct->contactgroups_ptr)
		mac->contactgroup_ptr = (contactgroup *)cntct->contactgroups_ptr->object_ptr;

	return OK;
}

int grab_contact_macros(contact *cntct)
{
	return grab_contact_macros_r(&global_macros, cntct);
}

static gboolean concat_custom_macro_value(gpointer _name, gpointer _hst, gpointer user_data)
{
	char *temp_buffer;
	host *temp_host = (host *)_hst;
	struct grab_custom_value_parameters *params = (struct grab_custom_value_parameters *)user_data;

	/* get the macro value for this host */
	grab_custom_macro_value_r(params->mac, params->macro_name, temp_host->name, NULL, &temp_buffer);

	if (temp_buffer == NULL)
		return FALSE;

	if (params->buffer->len > 0)
		g_string_append(params->buffer, params->delimiter);
	g_string_append(params->buffer, temp_buffer);
	return FALSE;
}

/* calculates the value of a custom macro */
static int grab_custom_macro_value_r(nagios_macros *mac, char *macro_name, char *arg1, char *arg2, char **output)
{
	host *temp_host = NULL;
	hostgroup *temp_hostgroup = NULL;
	service *temp_service = NULL;
	servicegroup *temp_servicegroup = NULL;
	servicesmember *temp_servicesmember = NULL;
	contact *temp_contact = NULL;
	contactgroup *temp_contactgroup = NULL;
	contactsmember *temp_contactsmember = NULL;
	int delimiter_len = 0;
	char *temp_buffer = NULL;
	int result = OK;

	if (macro_name == NULL || output == NULL)
		return ERROR;

	/***** CUSTOM HOST MACRO *****/
	if (strstr(macro_name, "_HOST") == macro_name) {

		/* a standard host macro */
		if (arg2 == NULL) {

			/* find the host for on-demand macros */
			if (arg1) {
				if ((temp_host = find_host(arg1)) == NULL)
					return ERROR;
			}

			/* else use saved host pointer */
			else if ((temp_host = mac->host_ptr) == NULL)
				return ERROR;

			/* get the host macro value */
			result = grab_custom_object_macro_r(mac, macro_name + 5, temp_host->custom_variables, output);
		}

		/* a host macro with a hostgroup name and delimiter */
		else {
			struct grab_custom_value_parameters params;

			if ((temp_hostgroup = find_hostgroup(arg1)) == NULL)
				return ERROR;

			params.mac = mac;
			params.macro_name = macro_name;
			params.buffer = g_string_new("");
			params.delimiter = arg2;

			/* can this ever trigger? */
			if (*output) {
				g_string_append(params.buffer, *output);
				nm_free(*output);
			}

			g_tree_foreach(temp_hostgroup->members, concat_custom_macro_value, &params);
			*output = nm_malloc(params.buffer->len + 1);
			strncpy(*output, params.buffer->str, params.buffer->len);
			(*output)[params.buffer->len] = 0;
			g_string_free(params.buffer, TRUE);
		}
	}

	/***** CUSTOM SERVICE MACRO *****/
	else if (strstr(macro_name, "_SERVICE") == macro_name) {

		/* use saved service pointer */
		if (arg1 == NULL && arg2 == NULL) {

			if ((temp_service = mac->service_ptr) == NULL)
				return ERROR;

			/* get the service macro value */
			result = grab_custom_object_macro_r(mac, macro_name + 8, temp_service->custom_variables, output);
		}

		/* else and ondemand macro... */
		else {

			/* if first arg is blank, it means use the current host name */
			if (mac->host_ptr == NULL)
				return ERROR;
			if ((temp_service = find_service((mac->host_ptr) ? mac->host_ptr->name : NULL, arg2))) {

				/* get the service macro value */
				result = grab_custom_object_macro_r(mac, macro_name + 8, temp_service->custom_variables, output);
			}

			/* else we have a service macro with a servicegroup name and a delimiter... */
			else {

				if ((temp_servicegroup = find_servicegroup(arg1)) == NULL)
					return ERROR;

				delimiter_len = strlen(arg2);

				/* concatenate macro values for all servicegroup members */
				for (temp_servicesmember = temp_servicegroup->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {

					temp_service = temp_servicesmember->service_ptr;

					/* get the macro value for this service */
					grab_custom_macro_value_r(mac, macro_name, temp_service->host_name, temp_service->description, &temp_buffer);

					if (temp_buffer == NULL)
						continue;

					/* add macro value to already running macro */
					if (*output == NULL)
						*output = nm_strdup(temp_buffer);
					else {
						*output = nm_realloc(*output, strlen(*output) + strlen(temp_buffer) + delimiter_len + 1);
						strcat(*output, arg2);
						strcat(*output, temp_buffer);
					}
					nm_free(temp_buffer);
				}
			}
		}
	}

	/***** CUSTOM CONTACT VARIABLE *****/
	else if (strstr(macro_name, "_CONTACT") == macro_name) {

		/* a standard contact macro */
		if (arg2 == NULL) {

			/* find the contact for on-demand macros */
			if (arg1) {
				if ((temp_contact = find_contact(arg1)) == NULL)
					return ERROR;
			}

			/* else use saved contact pointer */
			else if ((temp_contact = mac->contact_ptr) == NULL)
				return ERROR;

			/* get the contact macro value */
			result = grab_custom_object_macro_r(mac, macro_name + 8, temp_contact->custom_variables, output);
		}

		/* a contact macro with a contactgroup name and delimiter */
		else {

			if ((temp_contactgroup = find_contactgroup(arg1)) == NULL)
				return ERROR;

			delimiter_len = strlen(arg2);

			/* concatenate macro values for all contactgroup members */
			for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {

				if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
					continue;

				/* get the macro value for this contact */
				grab_custom_macro_value_r(mac, macro_name, temp_contact->name, NULL, &temp_buffer);

				if (temp_buffer == NULL)
					continue;

				/* add macro value to already running macro */
				if (*output == NULL)
					*output = nm_strdup(temp_buffer);
				else {
					*output = nm_realloc(*output, strlen(*output) + strlen(temp_buffer) + delimiter_len + 1);
					strcat(*output, arg2);
					strcat(*output, temp_buffer);
				}
				nm_free(temp_buffer);
			}
		}
	}

	else
		return ERROR;

	return result;
}

int grab_custom_macro_value(char *macro_name, char *arg1, char *arg2, char **output)
{
	return grab_custom_macro_value_r(&global_macros, macro_name, arg1, arg2, output);
}


/* calculates a date/time macro */
int grab_datetime_macro_r(nagios_macros *mac, int macro_type, char *arg1, char *arg2, char **output)
{
	time_t current_time = 0L;
	timeperiod *temp_timeperiod = NULL;
	time_t test_time = 0L;
	time_t next_valid_time = 0L;

	if (output == NULL)
		return ERROR;

	/* get the current time */
	time(&current_time);

	/* parse args, do prep work */
	switch (macro_type) {

	case MACRO_ISVALIDTIME:
	case MACRO_NEXTVALIDTIME:

		/* find the timeperiod */
		if ((temp_timeperiod = find_timeperiod(arg1)) == NULL)
			return ERROR;

		/* what timestamp should we use? */
		if (arg2)
			test_time = (time_t)strtoul(arg2, NULL, 0);
		else
			test_time = current_time;
		break;

	default:
		break;
	}

	/* calculate the value */
	switch (macro_type) {

	case MACRO_LONGDATETIME:
		if (*output == NULL)
			*output = nm_malloc(MAX_DATETIME_LENGTH);
		if (*output)
			get_datetime_string(&current_time, *output, MAX_DATETIME_LENGTH, LONG_DATE_TIME);
		break;

	case MACRO_SHORTDATETIME:
		if (*output == NULL)
			*output = nm_malloc(MAX_DATETIME_LENGTH);
		if (*output)
			get_datetime_string(&current_time, *output, MAX_DATETIME_LENGTH, SHORT_DATE_TIME);
		break;

	case MACRO_DATE:
		if (*output == NULL)
			*output = nm_malloc(MAX_DATETIME_LENGTH);
		if (*output)
			get_datetime_string(&current_time, *output, MAX_DATETIME_LENGTH, SHORT_DATE);
		break;

	case MACRO_TIME:
		if (*output == NULL)
			*output = nm_malloc(MAX_DATETIME_LENGTH);
		if (*output)
			get_datetime_string(&current_time, *output, MAX_DATETIME_LENGTH, SHORT_TIME);
		break;

	case MACRO_TIMET:
		nm_asprintf(output, "%lu", (unsigned long)current_time);
		break;

	case MACRO_ISVALIDTIME:
		nm_asprintf(output, "%d", (check_time_against_period(test_time, temp_timeperiod) == OK) ? 1 : 0);
		break;

	case MACRO_NEXTVALIDTIME:
		get_next_valid_time(test_time, &next_valid_time, temp_timeperiod);
		if (next_valid_time == test_time && check_time_against_period(test_time, temp_timeperiod) == ERROR)
			next_valid_time = (time_t)0L;
		nm_asprintf(output, "%lu", (unsigned long)next_valid_time);
		break;

	default:
		return ERROR;
		break;
	}

	return OK;
}

int grab_datetime_macro(int macro_type, char *arg1, char *arg2, char **output)
{
	return grab_datetime_macro_r(&global_macros, macro_type, arg1, arg2, output);
}


/* calculates a host macro */
static int grab_standard_host_macro_r(nagios_macros *mac, int macro_type, host *temp_host, char **output, int *free_macro)
{
	char *temp_buffer = NULL;
	hostgroup *temp_hostgroup = NULL;
	servicesmember *temp_servicesmember = NULL;
	service *temp_service = NULL;
	objectlist *temp_objectlist = NULL;
	time_t current_time = 0L;
	unsigned long duration = 0L;
	char *buf1 = NULL;
	char *buf2 = NULL;
	int total_host_services = 0;
	int total_host_services_ok = 0;
	int total_host_services_warning = 0;
	int total_host_services_unknown = 0;
	int total_host_services_critical = 0;

	if (temp_host == NULL || output == NULL || free_macro == NULL)
		return ERROR;

	/* get the macro */
	switch (macro_type) {

	case MACRO_HOSTNAME:
		*output = temp_host->name;
		break;
	case MACRO_HOSTDISPLAYNAME:
		if (temp_host->display_name)
			*output = temp_host->display_name;
		break;
	case MACRO_HOSTALIAS:
		*output = temp_host->alias;
		break;
	case MACRO_HOSTADDRESS:
		*output = temp_host->address;
		break;
	case MACRO_HOSTSTATE:
		*output = (char *)host_state_name(temp_host->current_state);
		break;
	case MACRO_HOSTSTATEID:
		*output = (char *)mkstr("%d", temp_host->current_state);
		break;
	case MACRO_LASTHOSTSTATE:
		*output = (char *)host_state_name(temp_host->last_state);
		break;
	case MACRO_LASTHOSTSTATEID:
		*output = (char *)mkstr("%d", temp_host->last_state);
		break;
	case MACRO_HOSTCHECKTYPE:
		*output = (char *)check_type_name(temp_host->check_type);
		break;
	case MACRO_HOSTSTATETYPE:
		*output = (char *)state_type_name(temp_host->state_type);
		break;
	case MACRO_HOSTOUTPUT:
		if (temp_host->plugin_output)
			*output = temp_host->plugin_output;
		break;
	case MACRO_LONGHOSTOUTPUT:
		if (temp_host->long_plugin_output)
			*output = temp_host->long_plugin_output;
		break;
	case MACRO_HOSTPERFDATA:
		if (temp_host->perf_data)
			*output = temp_host->perf_data;
		break;
	case MACRO_HOSTCHECKCOMMAND:
		if (temp_host->check_command)
			*output = temp_host->check_command;
		break;
	case MACRO_HOSTATTEMPT:
		*output = (char *)mkstr("%d", temp_host->current_attempt);
		break;
	case MACRO_MAXHOSTATTEMPTS:
		*output = (char *)mkstr("%d", temp_host->max_attempts);
		break;
	case MACRO_HOSTDOWNTIME:
		*output = (char *)mkstr("%d", temp_host->scheduled_downtime_depth);
		break;
	case MACRO_HOSTPERCENTCHANGE:
		*output = (char *)mkstr("%.2f", temp_host->percent_state_change);
		break;
	case MACRO_HOSTDURATIONSEC:
	case MACRO_HOSTDURATION:
		time(&current_time);
		duration = (unsigned long)(current_time - temp_host->last_state_change);
		if (macro_type == MACRO_HOSTDURATIONSEC)
			*output = (char *)mkstr("%lu", duration);
		else {
			*output = (char *)mkstr("%s", duration_string(duration));
		}
		break;
	case MACRO_HOSTEXECUTIONTIME:
		*output = (char *)mkstr("%.3f", temp_host->execution_time);
		break;
	case MACRO_HOSTLATENCY:
		*output = (char *)mkstr("%.3f", temp_host->latency);
		break;
	case MACRO_LASTHOSTCHECK:
		*output = (char *)mkstr("%lu", (unsigned long)temp_host->last_check);
		break;
	case MACRO_LASTHOSTSTATECHANGE:
		*output = (char *)mkstr("%lu", (unsigned long)temp_host->last_state_change);
		break;
	case MACRO_LASTHOSTUP:
		*output = (char *)mkstr("%lu", (unsigned long)temp_host->last_time_up);
		break;
	case MACRO_LASTHOSTDOWN:
		*output = (char *)mkstr("%lu", (unsigned long)temp_host->last_time_down);
		break;
	case MACRO_LASTHOSTUNREACHABLE:
		*output = (char *)mkstr("%lu", (unsigned long)temp_host->last_time_unreachable);
		break;
	case MACRO_HOSTNOTIFICATIONNUMBER:
		*output = (char *)mkstr("%d", temp_host->current_notification_number);
		break;
	case MACRO_HOSTNOTIFICATIONID:
		*output = temp_host->current_notification_id;
		break;
	case MACRO_HOSTEVENTID:
		*output = (char *)mkstr("%lu", temp_host->current_event_id);
		break;
	case MACRO_LASTHOSTEVENTID:
		*output = (char *)mkstr("%lu", temp_host->last_event_id);
		break;
	case MACRO_HOSTPROBLEMID:
		if(temp_host->current_problem_id != NULL)
			*output = temp_host->current_problem_id;
		break;
	case MACRO_LASTHOSTPROBLEMID:
		if(temp_host->last_problem_id != NULL)
			*output = temp_host->last_problem_id;
		break;
	case MACRO_HOSTPROBLEMSTART:
		*output = (char *)mkstr("%lu", (unsigned long)temp_host->problem_start);
		break;
	case MACRO_HOSTPROBLEMEND:
		*output = (char *)mkstr("%lu", (unsigned long)temp_host->problem_end);
		break;
	case MACRO_HOSTPROBLEMDURATIONSEC:
	case MACRO_HOSTPROBLEMDURATION:
		if(temp_host->problem_end > 0) {
			duration = (unsigned long)(temp_host->problem_end - temp_host->problem_start);
		} else if(temp_host->problem_start > 0) {
			time(&current_time);
			duration = (unsigned long)(current_time - temp_host->problem_start);
		}
		if (macro_type == MACRO_HOSTPROBLEMDURATIONSEC)
			*output = (char *)mkstr("%lu", duration);
		else {
			*output = (char *)mkstr("%s", duration_string(duration));
		}
		break;
	case MACRO_HOSTACTIONURL:
		if (temp_host->action_url)
			*output = temp_host->action_url;
		break;
	case MACRO_HOSTNOTESURL:
		if (temp_host->notes_url)
			*output = temp_host->notes_url;
		break;
	case MACRO_HOSTNOTES:
		if (temp_host->notes)
			*output = temp_host->notes;
		break;
	case MACRO_HOSTGROUPNAMES:
		/* find all hostgroups this host is associated with */
		for (temp_objectlist = temp_host->hostgroups_ptr; temp_objectlist != NULL; temp_objectlist = temp_objectlist->next) {

			if ((temp_hostgroup = (hostgroup *)temp_objectlist->object_ptr) == NULL)
				continue;

			nm_asprintf(&buf1, "%s%s%s", (buf2) ? buf2 : "", (buf2) ? "," : "", temp_hostgroup->group_name);
			nm_free(buf2);
			buf2 = buf1;
		}
		if (buf2) {
			*output = nm_strdup(buf2);
			nm_free(buf2);
		}
		break;
	case MACRO_TOTALHOSTSERVICES:
	case MACRO_TOTALHOSTSERVICESOK:
	case MACRO_TOTALHOSTSERVICESWARNING:
	case MACRO_TOTALHOSTSERVICESUNKNOWN:
	case MACRO_TOTALHOSTSERVICESCRITICAL:

		/* generate host service summary macros (if they haven't already been computed) */
		if (mac->x[MACRO_TOTALHOSTSERVICES] == NULL) {

			for (temp_servicesmember = temp_host->services; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
				temp_service = temp_servicesmember->service_ptr;

				total_host_services++;

				switch (temp_service->current_state) {
				case STATE_OK:
					total_host_services_ok++;
					break;
				case STATE_WARNING:
					total_host_services_warning++;
					break;
				case STATE_UNKNOWN:
					total_host_services_unknown++;
					break;
				case STATE_CRITICAL:
					total_host_services_critical++;
					break;
				default:
					break;
				}
			}

			/* these macros are time-intensive to compute, and will likely be used together, so save them all for future use */
			mac->x[MACRO_TOTALHOSTSERVICES] = (char *)mkstr("%d", total_host_services);
			mac->x[MACRO_TOTALHOSTSERVICESOK] = (char *)mkstr("%d", total_host_services_ok);
			mac->x[MACRO_TOTALHOSTSERVICESWARNING] = (char *)mkstr("%d", total_host_services_warning);
			mac->x[MACRO_TOTALHOSTSERVICESUNKNOWN] = (char *)mkstr("%d", total_host_services_unknown);
			mac->x[MACRO_TOTALHOSTSERVICESCRITICAL] = (char *)mkstr("%d", total_host_services_critical);
		}

		/* return only the macro the user requested */
		*output = mac->x[macro_type];
		break;
	case MACRO_HOSTVALUE:
		*output = (char *)mkstr("%u", mac->host_ptr->hourly_value);
		break;
	case MACRO_SERVICEVALUE:
		*output = (char *)mkstr("%u", host_services_value(mac->host_ptr));
		break;
	case MACRO_PROBLEMVALUE:
		*output = (char *)mkstr("%u", mac->host_ptr->hourly_value + host_services_value(mac->host_ptr));
		break;

	/***************/
	/* MISC MACROS */
	/***************/
	case MACRO_HOSTACKAUTHOR:
	case MACRO_HOSTACKAUTHORNAME:
	case MACRO_HOSTACKAUTHORALIAS:
	case MACRO_HOSTACKCOMMENT:
		/* no need to do any more work - these are already precomputed elsewhere */
		/* NOTE: these macros won't work as on-demand macros */
		*output = mac->x[macro_type];
		break;

	default:
		log_debug_info(DEBUGL_MACROS, 0, "UNHANDLED HOST MACRO #%d! THIS IS A BUG!\n", macro_type);
		return ERROR;
		break;
	}

	/* post-processing */
	/* notes, notes URL and action URL macros may themselves contain macros, so process them... */
	switch (macro_type) {
	case MACRO_HOSTACTIONURL:
	case MACRO_HOSTNOTESURL:
		*free_macro = TRUE;
		process_macros_r(mac, *output, &temp_buffer, URL_ENCODE_MACRO_CHARS);
		*output = temp_buffer;
		break;
	case MACRO_HOSTNOTES:
		*free_macro = TRUE;
		process_macros_r(mac, *output, &temp_buffer, 0);
		*output = temp_buffer;
		break;
	default:
		break;
	}

	return OK;
}


/* computes a hostgroup macro */
static int grab_standard_hostgroup_macro_r(nagios_macros *mac, int macro_type, hostgroup *temp_hostgroup, char **output)
{
	char *temp_buffer = NULL;

	if (temp_hostgroup == NULL || output == NULL)
		return ERROR;

	/* get the macro value */
	switch (macro_type) {
	case MACRO_HOSTGROUPNAME:
		*output = temp_hostgroup->group_name;
		break;
	case MACRO_HOSTGROUPALIAS:
		if (temp_hostgroup->alias)
			*output = temp_hostgroup->alias;
		break;
	case MACRO_HOSTGROUPMEMBERS:
		nm_free(*output);
		*output = implode_hosttree(temp_hostgroup->members, ",");
		break;
	case MACRO_HOSTGROUPACTIONURL:
		if (temp_hostgroup->action_url)
			*output = temp_hostgroup->action_url;
		break;
	case MACRO_HOSTGROUPNOTESURL:
		if (temp_hostgroup->notes_url)
			*output = temp_hostgroup->notes_url;
		break;
	case MACRO_HOSTGROUPNOTES:
		if (temp_hostgroup->notes)
			*output = temp_hostgroup->notes;
		break;
	default:
		log_debug_info(DEBUGL_MACROS, 0, "UNHANDLED HOSTGROUP MACRO #%d! THIS IS A BUG!\n", macro_type);
		return ERROR;
		break;
	}

	/* post-processing */
	/* notes, notes URL and action URL macros may themselves contain macros, so process them... */
	switch (macro_type) {
	case MACRO_HOSTGROUPACTIONURL:
	case MACRO_HOSTGROUPNOTESURL:
		process_macros_r(mac, *output, &temp_buffer, URL_ENCODE_MACRO_CHARS);
		*output = temp_buffer;
		break;
	case MACRO_HOSTGROUPNOTES:
		process_macros_r(mac, *output, &temp_buffer, 0);
		*output = temp_buffer;
		break;
	default:
		break;
	}

	return OK;
}


/* computes a service macro */
static int grab_standard_service_macro_r(nagios_macros *mac, int macro_type, service *temp_service, char **output, int *free_macro)
{
	char *temp_buffer = NULL;
	servicegroup *temp_servicegroup = NULL;
	objectlist *temp_objectlist = NULL;
	time_t current_time = 0L;
	unsigned long duration = 0L;
	char *buf1 = NULL;
	char *buf2 = NULL;

	if (temp_service == NULL || output == NULL)
		return ERROR;

	/* get the macro value */
	switch (macro_type) {
	case MACRO_SERVICEDESC:
		*output = temp_service->description;
		break;
	case MACRO_SERVICEDISPLAYNAME:
		if (temp_service->display_name)
			*output = temp_service->display_name;
		break;
	case MACRO_SERVICEOUTPUT:
		if (temp_service->plugin_output)
			*output = temp_service->plugin_output;
		break;
	case MACRO_LONGSERVICEOUTPUT:
		if (temp_service->long_plugin_output)
			*output = temp_service->long_plugin_output;
		break;
	case MACRO_SERVICEPERFDATA:
		if (temp_service->perf_data)
			*output = temp_service->perf_data;
		break;
	case MACRO_SERVICECHECKCOMMAND:
		if (temp_service->check_command)
			*output = temp_service->check_command;
		break;
	case MACRO_SERVICECHECKTYPE:
		*output = (char *)check_type_name(temp_service->check_type);
		break;
	case MACRO_SERVICESTATETYPE:
		*output = (char *)state_type_name(temp_service->state_type);
		break;
	case MACRO_SERVICESTATE:
		*output = (char *)service_state_name(temp_service->current_state);
		break;
	case MACRO_SERVICESTATEID:
		*output = (char *)mkstr("%d", temp_service->current_state);
		break;
	case MACRO_LASTSERVICESTATE:
		*output = (char *)service_state_name(temp_service->last_state);
		break;
	case MACRO_LASTSERVICESTATEID:
		*output = (char *)mkstr("%d", temp_service->last_state);
		break;
	case MACRO_SERVICEISVOLATILE:
		*output = (char *)mkstr("%d", temp_service->is_volatile);
		break;
	case MACRO_SERVICEATTEMPT:
		*output = (char *)mkstr("%d", temp_service->current_attempt);
		break;
	case MACRO_MAXSERVICEATTEMPTS:
		*output = (char *)mkstr("%d", temp_service->max_attempts);
		break;
	case MACRO_SERVICEEXECUTIONTIME:
		*output = (char *)mkstr("%.3f", temp_service->execution_time);
		break;
	case MACRO_SERVICELATENCY:
		*output = (char *)mkstr("%.3f", temp_service->latency);
		break;
	case MACRO_LASTSERVICECHECK:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->last_check);
		break;
	case MACRO_LASTSERVICESTATECHANGE:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->last_state_change);
		break;
	case MACRO_LASTSERVICEOK:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->last_time_ok);
		break;
	case MACRO_LASTSERVICEWARNING:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->last_time_warning);
		break;
	case MACRO_LASTSERVICEUNKNOWN:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->last_time_unknown);
		break;
	case MACRO_LASTSERVICECRITICAL:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->last_time_critical);
		break;
	case MACRO_SERVICEDOWNTIME:
		*output = (char *)mkstr("%d", temp_service->scheduled_downtime_depth);
		break;
	case MACRO_SERVICEPERCENTCHANGE:
		*output = (char *)mkstr("%.2f", temp_service->percent_state_change);
		break;
	case MACRO_SERVICEDURATIONSEC:
	case MACRO_SERVICEDURATION:
		time(&current_time);
		duration = (unsigned long)(current_time - temp_service->last_state_change);
		if (macro_type == MACRO_SERVICEDURATIONSEC)
			*output = (char *)mkstr("%lu", duration);
		else {
			*output = (char *)mkstr("%s", duration_string(duration));
		}
		break;
	case MACRO_SERVICENOTIFICATIONNUMBER:
		*output = (char *)mkstr("%d", temp_service->current_notification_number);
		break;
	case MACRO_SERVICENOTIFICATIONID:
		*output = temp_service->current_notification_id;
		break;
	case MACRO_SERVICEEVENTID:
		*output = (char *)mkstr("%lu", temp_service->current_event_id);
		break;
	case MACRO_LASTSERVICEEVENTID:
		*output = (char *)mkstr("%lu", temp_service->last_event_id);
		break;
	case MACRO_SERVICEPROBLEMID:
		*output = temp_service->current_problem_id;
		break;
	case MACRO_LASTSERVICEPROBLEMID:
		*output = temp_service->last_problem_id;
		break;
	case MACRO_SERVICEPROBLEMSTART:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->problem_start);
		break;
	case MACRO_SERVICEPROBLEMEND:
		*output = (char *)mkstr("%lu", (unsigned long)temp_service->problem_end);
		break;
	case MACRO_SERVICEPROBLEMDURATIONSEC:
	case MACRO_SERVICEPROBLEMDURATION:
		if(temp_service->problem_end > 0) {
			duration = (unsigned long)(temp_service->problem_end - temp_service->problem_start);
		} else if(temp_service->problem_start > 0) {
			time(&current_time);
			duration = (unsigned long)(current_time - temp_service->problem_start);
		}
		if (macro_type == MACRO_SERVICEPROBLEMDURATIONSEC)
			*output = (char *)mkstr("%lu", duration);
		else {
			*output = (char *)mkstr("%s", duration_string(duration));
		}
		break;
	case MACRO_SERVICEACTIONURL:
		if (temp_service->action_url)
			*output = temp_service->action_url;
		break;
	case MACRO_SERVICENOTESURL:
		if (temp_service->notes_url)
			*output = temp_service->notes_url;
		break;
	case MACRO_SERVICENOTES:
		if (temp_service->notes)
			*output = temp_service->notes;
		break;
	case MACRO_SERVICEGROUPNAMES:
		/* find all servicegroups this service is associated with */
		for (temp_objectlist = temp_service->servicegroups_ptr; temp_objectlist != NULL; temp_objectlist = temp_objectlist->next) {

			if ((temp_servicegroup = (servicegroup *)temp_objectlist->object_ptr) == NULL)
				continue;

			nm_asprintf(&buf1, "%s%s%s", (buf2) ? buf2 : "", (buf2) ? "," : "", temp_servicegroup->group_name);
			nm_free(buf2);
			buf2 = buf1;
		}
		if (buf2) {
			*output = nm_strdup(buf2);
			nm_free(buf2);
		}
		break;
	/***************/
	/* MISC MACROS */
	/***************/
	case MACRO_SERVICEACKAUTHOR:
	case MACRO_SERVICEACKAUTHORNAME:
	case MACRO_SERVICEACKAUTHORALIAS:
	case MACRO_SERVICEACKCOMMENT:
		/* no need to do any more work - these are already precomputed elsewhere */
		/* NOTE: these macros won't work as on-demand macros */
		*output = mac->x[macro_type];
		*free_macro = FALSE;
		break;

	default:
		log_debug_info(DEBUGL_MACROS, 0, "UNHANDLED SERVICE MACRO #%d! THIS IS A BUG!\n", macro_type);
		return ERROR;
		break;
	}

	/* post-processing */
	/* notes, notes URL and action URL macros may themselves contain macros, so process them... */
	switch (macro_type) {
	case MACRO_SERVICEACTIONURL:
	case MACRO_SERVICENOTESURL:
		process_macros_r(mac, *output, &temp_buffer, URL_ENCODE_MACRO_CHARS);
		*output = temp_buffer;
		break;
	case MACRO_SERVICENOTES:
		process_macros_r(mac, *output, &temp_buffer, 0);
		*output = temp_buffer;
		break;
	default:
		break;
	}

	return OK;
}


/* computes a servicegroup macro */
static int grab_standard_servicegroup_macro_r(nagios_macros *mac, int macro_type, servicegroup *temp_servicegroup, char **output)
{
	servicesmember *temp_servicesmember = NULL;
	char *temp_buffer = NULL;
	unsigned int temp_len = 0;
	unsigned int init_len = 0;

	if (temp_servicegroup == NULL || output == NULL)
		return ERROR;

	/* get the macro value */
	switch (macro_type) {
	case MACRO_SERVICEGROUPNAME:
		*output = temp_servicegroup->group_name;
		break;
	case MACRO_SERVICEGROUPALIAS:
		if (temp_servicegroup->alias)
			*output = temp_servicegroup->alias;
		break;
	case MACRO_SERVICEGROUPMEMBERS:
		/* make the calculations for total string length */
		for (temp_servicesmember = temp_servicegroup->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
			if (temp_servicesmember->host_name == NULL || temp_servicesmember->service_description == NULL)
				continue;
			if (temp_len == 0) {
				temp_len += strlen(temp_servicesmember->host_name) + strlen(temp_servicesmember->service_description) + 2;
			} else {
				temp_len += strlen(temp_servicesmember->host_name) + strlen(temp_servicesmember->service_description) + 3;
			}
		}
		if (!temp_len) {
			/* empty group, so return the nul string */
			*output = nm_calloc(1, 1);
			return OK;
		}
		/* allocate or reallocate the memory buffer */
		if (*output == NULL) {
			*output = nm_malloc(temp_len);
		} else {
			init_len = strlen(*output);
			temp_len += init_len;
			*output = nm_realloc(*output, temp_len);
		}
		/* now fill in the string with the group members */
		for (temp_servicesmember = temp_servicegroup->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
			if (temp_servicesmember->host_name == NULL || temp_servicesmember->service_description == NULL)
				continue;
			temp_buffer = *output + init_len;
			if (init_len == 0) { /* If our buffer didn't contain anything, we just need to write "%s,%s" */
				init_len += sprintf(temp_buffer, "%s,%s", temp_servicesmember->host_name, temp_servicesmember->service_description);
			} else { /* Now we need to write ",%s,%s" */
				init_len += sprintf(temp_buffer, ",%s,%s", temp_servicesmember->host_name, temp_servicesmember->service_description);
			}
		}
		break;
	case MACRO_SERVICEGROUPACTIONURL:
		if (temp_servicegroup->action_url)
			*output = temp_servicegroup->action_url;
		break;
	case MACRO_SERVICEGROUPNOTESURL:
		if (temp_servicegroup->notes_url)
			*output = temp_servicegroup->notes_url;
		break;
	case MACRO_SERVICEGROUPNOTES:
		if (temp_servicegroup->notes)
			*output = temp_servicegroup->notes;
		break;
	default:
		log_debug_info(DEBUGL_MACROS, 0, "UNHANDLED SERVICEGROUP MACRO #%d! THIS IS A BUG!\n", macro_type);
		return ERROR;
	}

	/* post-processing */
	/* notes, notes URL and action URL macros may themselves contain macros, so process them... */
	switch (macro_type) {
	case MACRO_SERVICEGROUPACTIONURL:
	case MACRO_SERVICEGROUPNOTESURL:
		process_macros_r(mac, *output, &temp_buffer, URL_ENCODE_MACRO_CHARS);
		*output = temp_buffer;
		break;
	case MACRO_SERVICEGROUPNOTES:
		process_macros_r(mac, *output, &temp_buffer, 0);
		*output = temp_buffer;
		break;
	default:
		break;
	}

	return OK;
}


/* computes a contact macro */
static int grab_standard_contact_macro_r(nagios_macros *mac, int macro_type, contact *temp_contact, char **output)
{
	contactgroup *temp_contactgroup = NULL;
	objectlist *temp_objectlist = NULL;
	char *buf1 = NULL;
	char *buf2 = NULL;

	if (temp_contact == NULL || output == NULL)
		return ERROR;

	/* get the macro value */
	switch (macro_type) {
	case MACRO_CONTACTNAME:
		*output = temp_contact->name;
		break;
	case MACRO_CONTACTALIAS:
		*output = temp_contact->alias;
		break;
	case MACRO_CONTACTEMAIL:
		if (temp_contact->email)
			*output = temp_contact->email;
		break;
	case MACRO_CONTACTPAGER:
		if (temp_contact->pager)
			*output = temp_contact->pager;
		break;
	case MACRO_CONTACTGROUPNAMES:
		/* get the contactgroup names */
		/* find all contactgroups this contact is a member of */
		for (temp_objectlist = temp_contact->contactgroups_ptr; temp_objectlist != NULL; temp_objectlist = temp_objectlist->next) {

			if ((temp_contactgroup = (contactgroup *)temp_objectlist->object_ptr) == NULL)
				continue;

			nm_asprintf(&buf1, "%s%s%s", (buf2) ? buf2 : "", (buf2) ? "," : "", temp_contactgroup->group_name);
			nm_free(buf2);
			buf2 = buf1;
		}
		if (buf2) {
			*output = nm_strdup(buf2);
			nm_free(buf2);
		}
		break;
	default:
		log_debug_info(DEBUGL_MACROS, 0, "UNHANDLED CONTACT MACRO #%d! THIS IS A BUG!\n", macro_type);
		return ERROR;
	}

	return OK;
}


/* computes a contact address macro */
static int grab_contact_address_macro(int macro_num, contact *temp_contact, char **output)
{
	if (macro_num < 0 || macro_num >= MAX_CONTACT_ADDRESSES)
		return ERROR;

	if (temp_contact == NULL || output == NULL)
		return ERROR;

	/* get the macro */
	if (temp_contact->address[macro_num])
		*output = temp_contact->address[macro_num];

	return OK;
}


/* computes a contactgroup macro */
static int grab_standard_contactgroup_macro(int macro_type, contactgroup *temp_contactgroup, char **output)
{
	contactsmember *temp_contactsmember = NULL;

	if (temp_contactgroup == NULL || output == NULL)
		return ERROR;

	/* get the macro value */
	switch (macro_type) {
	case MACRO_CONTACTGROUPNAME:
		*output = temp_contactgroup->group_name;
		break;
	case MACRO_CONTACTGROUPALIAS:
		if (temp_contactgroup->alias)
			*output = temp_contactgroup->alias;
		break;
	case MACRO_CONTACTGROUPMEMBERS:
		/* get the member list */
		for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {
			if (temp_contactsmember->contact_name == NULL)
				continue;
			if (*output == NULL)
				*output = nm_strdup(temp_contactsmember->contact_name);
			*output = nm_realloc(*output, strlen(*output) + strlen(temp_contactsmember->contact_name) + 2);
			strcat(*output, ",");
			strcat(*output, temp_contactsmember->contact_name);
		}
		break;
	default:
		log_debug_info(DEBUGL_MACROS, 0, "UNHANDLED CONTACTGROUP MACRO #%d! THIS IS A BUG!\n", macro_type);
		return ERROR;
	}

	return OK;
}


/******************************************************************/
/********************* MACRO STRING FUNCTIONS *********************/
/******************************************************************/

/* cleans illegal characters in macros before output */
static char *clean_macro_chars(char *macro, int options)
{
	register int x = 0;
	register int y = 0;
	register int ch = 0;
	register int len = 0;
	char *ret = NULL;

	if (macro == NULL || !*macro)
		return "";

	len = (int)strlen(macro);
	ret = nm_strdup(macro);

	/* strip illegal characters out of macro */
	if (options & STRIP_ILLEGAL_MACRO_CHARS) {
		for (y = 0, x = 0; x < len; x++) {
			ch = macro[x] & 0xff;

			/* illegal chars are skipped */
			if (!illegal_output_char_map[ch])
				ret[y++] = ret[x];
		}

		ret[y++] = '\x0';
	}

	return ret;
}


/* encodes a string in proper URL format */
static char *get_url_encoded_string(char *input)
{
	/* From RFC 3986:
	segment       = *pchar

	[...]

	pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"

	query         = *( pchar / "/" / "?" )

	fragment      = *( pchar / "/" / "?" )

	pct-encoded   = "%" HEXDIG HEXDIG

	unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
	reserved      = gen-delims / sub-delims
	gen-delims    = ":" / "/" / "?" / "#" / "[" / "]" / "@"
	sub-delims    = "!" / "$" / "&" / "'" / "(" / ")"
	                 / "*" / "+" / "," / ";" / "="

	Encode everything but "unreserved", to be on safe side.

	Another note:
	nowhere in the RFC states that + is interpreted as space. Therefore, encode
	space as %20 (as all other characters that should be escaped)
	*/

	register int x = 0;
	register int y = 0;
	char *encoded_url_string = NULL;


	/* bail if no input */
	if (input == NULL)
		return NULL;

	/* allocate enough memory to escape all characters if necessary */
	encoded_url_string = nm_malloc((strlen(input) * 3) + 1);
	/* check/encode all characters */
	for (x = 0, y = 0; input[x]; x++) {

		/* alpha-numeric characters and a few other characters don't get encoded */
		if (((char)input[x] >= '0' && (char)input[x] <= '9') ||
		    ((char)input[x] >= 'A' && (char)input[x] <= 'Z') ||
		    ((char)input[x] >= 'a' && (char)input[x] <= 'z') ||
		    (char)input[x] == '.' ||
		    (char)input[x] == '-' ||
		    (char)input[x] == '_' ||
		    (char)input[x] == '~') {
			encoded_url_string[y++] = input[x];
		}

		/* anything else gets represented by its hex value */
		else {
			sprintf(&encoded_url_string[y], "%%%02X", (unsigned int)(input[x] & 0xFF));
			y += 3;
		}
	}

	/* terminate encoded string */
	encoded_url_string[y] = '\x0';

	return encoded_url_string;
}


static gboolean concat_macrox_value(gpointer _name, gpointer _hst, gpointer user_data)
{
	int free_sub_macro = FALSE;
	char *temp_buffer = NULL;
	host *temp_host = (host *)_hst;
	struct grab_macro_value_parameters *params = (struct grab_macro_value_parameters *)user_data;

	grab_standard_host_macro_r(params->mac, params->macro_type, temp_host, &temp_buffer, &free_sub_macro);

	if (temp_buffer == NULL)
		return FALSE;

	if (params->buffer->len > 0)
		g_string_append(params->buffer, params->delimiter);
	g_string_append(params->buffer, temp_buffer);

	if (free_sub_macro == TRUE)
		nm_free(temp_buffer);

	return FALSE;
}

static int grab_macrox_value_r(nagios_macros *mac, int macro_type, char *arg1, char *arg2, char **output, int *free_macro)
{
	host *temp_host = NULL;
	hostgroup *temp_hostgroup = NULL;
	service *temp_service = NULL;
	servicegroup *temp_servicegroup = NULL;
	servicesmember *temp_servicesmember = NULL;
	contact *temp_contact = NULL;
	contactgroup *temp_contactgroup = NULL;
	contactsmember *temp_contactsmember = NULL;
	char *temp_buffer = NULL;
	int result = OK;
	int delimiter_len = 0;
	int free_sub_macro = FALSE;
	register int x;
	int authorized = TRUE;
	int problem = TRUE;
	int hosts_up = 0;
	int hosts_down = 0;
	int hosts_unreachable = 0;
	int hosts_down_unhandled = 0;
	int hosts_unreachable_unhandled = 0;
	int host_problems = 0;
	int host_problems_unhandled = 0;
	int services_ok = 0;
	int services_warning = 0;
	int services_unknown = 0;
	int services_critical = 0;
	int services_warning_unhandled = 0;
	int services_unknown_unhandled = 0;
	int services_critical_unhandled = 0;
	int service_problems = 0;
	int service_problems_unhandled = 0;

	if (output == NULL || free_macro == NULL)
		return ERROR;

	*free_macro = FALSE;

	/* handle the macro */
	switch (macro_type) {

	/***************/
	/* HOST MACROS */
	/***************/
	case MACRO_HOSTGROUPNAMES:
		*free_macro = TRUE;
	/* FALLTHROUGH */
	case MACRO_HOSTNAME:
	case MACRO_HOSTALIAS:
	case MACRO_HOSTADDRESS:
	case MACRO_LASTHOSTCHECK:
	case MACRO_LASTHOSTSTATECHANGE:
	case MACRO_HOSTOUTPUT:
	case MACRO_HOSTPERFDATA:
	case MACRO_HOSTSTATE:
	case MACRO_HOSTSTATEID:
	case MACRO_HOSTATTEMPT:
	case MACRO_HOSTEXECUTIONTIME:
	case MACRO_HOSTLATENCY:
	case MACRO_HOSTDURATION:
	case MACRO_HOSTDURATIONSEC:
	case MACRO_HOSTDOWNTIME:
	case MACRO_HOSTSTATETYPE:
	case MACRO_HOSTPERCENTCHANGE:
	case MACRO_HOSTACKAUTHOR:
	case MACRO_HOSTACKCOMMENT:
	case MACRO_LASTHOSTUP:
	case MACRO_LASTHOSTDOWN:
	case MACRO_LASTHOSTUNREACHABLE:
	case MACRO_HOSTCHECKCOMMAND:
	case MACRO_HOSTDISPLAYNAME:
	case MACRO_HOSTACTIONURL:
	case MACRO_HOSTNOTESURL:
	case MACRO_HOSTNOTES:
	case MACRO_HOSTCHECKTYPE:
	case MACRO_LONGHOSTOUTPUT:
	case MACRO_HOSTNOTIFICATIONNUMBER:
	case MACRO_HOSTNOTIFICATIONID:
	case MACRO_HOSTEVENTID:
	case MACRO_LASTHOSTEVENTID:
	case MACRO_HOSTACKAUTHORNAME:
	case MACRO_HOSTACKAUTHORALIAS:
	case MACRO_MAXHOSTATTEMPTS:
	case MACRO_TOTALHOSTSERVICES:
	case MACRO_TOTALHOSTSERVICESOK:
	case MACRO_TOTALHOSTSERVICESWARNING:
	case MACRO_TOTALHOSTSERVICESUNKNOWN:
	case MACRO_TOTALHOSTSERVICESCRITICAL:
	case MACRO_HOSTPROBLEMID:
	case MACRO_LASTHOSTPROBLEMID:
	case MACRO_LASTHOSTSTATE:
	case MACRO_LASTHOSTSTATEID:
	case MACRO_HOSTPROBLEMSTART:
	case MACRO_HOSTPROBLEMEND:
	case MACRO_HOSTPROBLEMDURATIONSEC:
	case MACRO_HOSTPROBLEMDURATION:


		/* a standard host macro */
		if (arg2 == NULL) {

			/* find the host for on-demand macros */
			if (arg1) {
				if ((temp_host = find_host(arg1)) == NULL)
					return ERROR;
			}

			/* else use saved host pointer */
			else if ((temp_host = mac->host_ptr) == NULL)
				return ERROR;

			/* get the host macro value */
			result = grab_standard_host_macro_r(mac, macro_type, temp_host, output, free_macro);
		}

		/* a host macro with a hostgroup name and delimiter */
		else {
			struct grab_macro_value_parameters params;

			if ((temp_hostgroup = find_hostgroup(arg1)) == NULL)
				return ERROR;

			params.mac = mac;
			params.macro_type = macro_type;
			params.buffer = g_string_new("");
			params.delimiter = arg2;

			/* can this ever trigger? */
			if (*output) {
				g_string_append(params.buffer, *output);
				nm_free(*output);
			}

			g_tree_foreach(temp_hostgroup->members, concat_macrox_value, &params);
			*output = nm_malloc(params.buffer->len + 1);
			strncpy(*output, params.buffer->str, params.buffer->len);
			(*output)[params.buffer->len] = 0;
			g_string_free(params.buffer, TRUE);
		}
		break;

	/********************/
	/* HOSTGROUP MACROS */
	/********************/
	case MACRO_HOSTGROUPMEMBERS:
		*free_macro = TRUE;
	/* FALLTHROUGH */
	case MACRO_HOSTGROUPNAME:
	case MACRO_HOSTGROUPALIAS:
	case MACRO_HOSTGROUPNOTES:
	case MACRO_HOSTGROUPNOTESURL:
	case MACRO_HOSTGROUPACTIONURL:

		/* a standard hostgroup macro */
		/* use the saved hostgroup pointer */
		if (arg1 == NULL) {
			if ((temp_hostgroup = mac->hostgroup_ptr) == NULL)
				return ERROR;
		}

		/* else find the hostgroup for on-demand macros */
		else {
			if ((temp_hostgroup = find_hostgroup(arg1)) == NULL)
				return ERROR;
		}

		/* get the hostgroup macro value */
		result = grab_standard_hostgroup_macro_r(mac, macro_type, temp_hostgroup, output);
		break;

	/******************/
	/* SERVICE MACROS */
	/******************/
	case MACRO_SERVICEGROUPNAMES:
		*free_macro = TRUE;
	/* FALLTHROUGH */
	case MACRO_SERVICEDESC:
	case MACRO_SERVICESTATE:
	case MACRO_SERVICESTATEID:
	case MACRO_SERVICEATTEMPT:
	case MACRO_LASTSERVICECHECK:
	case MACRO_LASTSERVICESTATECHANGE:
	case MACRO_SERVICEOUTPUT:
	case MACRO_SERVICEPERFDATA:
	case MACRO_SERVICEEXECUTIONTIME:
	case MACRO_SERVICELATENCY:
	case MACRO_SERVICEDURATION:
	case MACRO_SERVICEDURATIONSEC:
	case MACRO_SERVICEDOWNTIME:
	case MACRO_SERVICESTATETYPE:
	case MACRO_SERVICEPERCENTCHANGE:
	case MACRO_SERVICEACKAUTHOR:
	case MACRO_SERVICEACKCOMMENT:
	case MACRO_LASTSERVICEOK:
	case MACRO_LASTSERVICEWARNING:
	case MACRO_LASTSERVICEUNKNOWN:
	case MACRO_LASTSERVICECRITICAL:
	case MACRO_SERVICECHECKCOMMAND:
	case MACRO_SERVICEDISPLAYNAME:
	case MACRO_SERVICEACTIONURL:
	case MACRO_SERVICENOTESURL:
	case MACRO_SERVICENOTES:
	case MACRO_SERVICECHECKTYPE:
	case MACRO_LONGSERVICEOUTPUT:
	case MACRO_SERVICENOTIFICATIONNUMBER:
	case MACRO_SERVICENOTIFICATIONID:
	case MACRO_SERVICEEVENTID:
	case MACRO_LASTSERVICEEVENTID:
	case MACRO_SERVICEACKAUTHORNAME:
	case MACRO_SERVICEACKAUTHORALIAS:
	case MACRO_MAXSERVICEATTEMPTS:
	case MACRO_SERVICEISVOLATILE:
	case MACRO_SERVICEPROBLEMID:
	case MACRO_LASTSERVICEPROBLEMID:
	case MACRO_LASTSERVICESTATE:
	case MACRO_LASTSERVICESTATEID:
	case MACRO_SERVICEPROBLEMSTART:
	case MACRO_SERVICEPROBLEMEND:
	case MACRO_SERVICEPROBLEMDURATIONSEC:
	case MACRO_SERVICEPROBLEMDURATION:

		/* use saved service pointer */
		if (arg1 == NULL && arg2 == NULL) {

			if ((temp_service = mac->service_ptr) == NULL)
				return ERROR;

			result = grab_standard_service_macro_r(mac, macro_type, temp_service, output, free_macro);
		}

		/* else and ondemand macro... */
		else {

			/* if first arg is blank, it means use the current host name */
			if (arg1 == NULL || arg1[0] == '\x0') {

				if (mac->host_ptr == NULL)
					return ERROR;

				if ((temp_service = find_service(mac->host_ptr->name, arg2))) {

					/* get the service macro value */
					result = grab_standard_service_macro_r(mac, macro_type, temp_service, output, free_macro);
				}
			}

			/* on-demand macro with both host and service name */
			else if ((temp_service = find_service(arg1, arg2))) {

				/* get the service macro value */
				result = grab_standard_service_macro_r(mac, macro_type, temp_service, output, free_macro);
			}

			/* else we have a service macro with a servicegroup name and a delimiter... */
			else if (arg1 && arg2) {

				if ((temp_servicegroup = find_servicegroup(arg1)) == NULL)
					return ERROR;

				delimiter_len = strlen(arg2);
				*free_macro = TRUE;

				/* concatenate macro values for all servicegroup members */
				for (temp_servicesmember = temp_servicegroup->members; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {

					temp_service = temp_servicesmember->service_ptr;

					/* get the macro value for this service */
					grab_standard_service_macro_r(mac, macro_type, temp_service, &temp_buffer, &free_sub_macro);

					if (temp_buffer == NULL)
						continue;

					/* add macro value to already running macro */
					if (*output == NULL)
						*output = nm_strdup(temp_buffer);
					else {
						*output = nm_realloc(*output, strlen(*output) + strlen(temp_buffer) + delimiter_len + 1);
						strcat(*output, arg2);
						strcat(*output, temp_buffer);
					}
					if (free_sub_macro == TRUE)
						nm_free(temp_buffer);
				}
			} else
				return ERROR;
		}
		break;

	/***********************/
	/* SERVICEGROUP MACROS */
	/***********************/
	case MACRO_SERVICEGROUPMEMBERS:
	case MACRO_SERVICEGROUPNOTES:
	case MACRO_SERVICEGROUPNOTESURL:
	case MACRO_SERVICEGROUPACTIONURL:
		*free_macro = TRUE;
	/* FALLTHROUGH */
	case MACRO_SERVICEGROUPNAME:
	case MACRO_SERVICEGROUPALIAS:
		/* a standard servicegroup macro */
		/* use the saved servicegroup pointer */
		if (arg1 == NULL) {
			if ((temp_servicegroup = mac->servicegroup_ptr) == NULL)
				return ERROR;
		}

		/* else find the servicegroup for on-demand macros */
		else {
			if ((temp_servicegroup = find_servicegroup(arg1)) == NULL)
				return ERROR;
		}

		/* get the servicegroup macro value */
		result = grab_standard_servicegroup_macro_r(mac, macro_type, temp_servicegroup, output);
		break;

	/******************/
	/* CONTACT MACROS */
	/******************/
	case MACRO_CONTACTGROUPNAMES:
		*free_macro = TRUE;
	/* FALLTHROUGH */
	case MACRO_CONTACTNAME:
	case MACRO_CONTACTALIAS:
	case MACRO_CONTACTEMAIL:
	case MACRO_CONTACTPAGER:
		/* a standard contact macro */
		if (arg2 == NULL) {

			/* find the contact for on-demand macros */
			if (arg1) {
				if ((temp_contact = find_contact(arg1)) == NULL)
					return ERROR;
			}

			/* else use saved contact pointer */
			else if ((temp_contact = mac->contact_ptr) == NULL)
				return ERROR;

			/* get the contact macro value */
			result = grab_standard_contact_macro_r(mac, macro_type, temp_contact, output);
		}

		/* a contact macro with a contactgroup name and delimiter */
		else {

			if ((temp_contactgroup = find_contactgroup(arg1)) == NULL)
				return ERROR;

			delimiter_len = strlen(arg2);
			*free_macro = TRUE;

			/* concatenate macro values for all contactgroup members */
			for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {

				if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
					continue;

				/* get the macro value for this contact */
				grab_standard_contact_macro_r(mac, macro_type, temp_contact, &temp_buffer);

				if (temp_buffer == NULL)
					continue;

				/* add macro value to already running macro */
				if (*output == NULL)
					*output = nm_strdup(temp_buffer);
				else {
					*output = nm_realloc(*output, strlen(*output) + strlen(temp_buffer) + delimiter_len + 1);
					strcat(*output, arg2);
					strcat(*output, temp_buffer);
				}
				nm_free(temp_buffer);
			}
		}
		break;

	/***********************/
	/* CONTACTGROUP MACROS */
	/***********************/
	case MACRO_CONTACTGROUPMEMBERS:
		*free_macro = TRUE;
	/* FALLTHROUGH */
	case MACRO_CONTACTGROUPNAME:
	case MACRO_CONTACTGROUPALIAS:
		/* a standard contactgroup macro */
		/* use the saved contactgroup pointer */
		if (arg1 == NULL) {
			if ((temp_contactgroup = mac->contactgroup_ptr) == NULL)
				return ERROR;
		}

		/* else find the contactgroup for on-demand macros */
		else {
			if ((temp_contactgroup = find_contactgroup(arg1)) == NULL)
				return ERROR;
		}

		/* get the contactgroup macro value */
		result = grab_standard_contactgroup_macro(macro_type, temp_contactgroup, output);
		break;

	/***********************/
	/* NOTIFICATION MACROS */
	/***********************/
	case MACRO_NOTIFICATIONTYPE:
	case MACRO_NOTIFICATIONNUMBER:
	case MACRO_NOTIFICATIONRECIPIENTS:
	case MACRO_NOTIFICATIONISESCALATED:
	case MACRO_NOTIFICATIONAUTHOR:
	case MACRO_NOTIFICATIONAUTHORNAME:
	case MACRO_NOTIFICATIONAUTHORALIAS:
	case MACRO_NOTIFICATIONCOMMENT:

		/* notification macros have already been pre-computed */
		*output = mac->x[macro_type];
		*free_macro = FALSE;
		break;

	/********************/
	/* DATE/TIME MACROS */
	/********************/
	case MACRO_LONGDATETIME:
	case MACRO_SHORTDATETIME:
	case MACRO_DATE:
	case MACRO_TIME:
	case MACRO_TIMET:
	case MACRO_ISVALIDTIME:
	case MACRO_NEXTVALIDTIME:

		/* calculate macros */
		result = grab_datetime_macro_r(mac, macro_type, arg1, arg2, output);
		*free_macro = TRUE;
		break;

	/*****************/
	/* STATIC MACROS */
	/*****************/
	case MACRO_ADMINEMAIL:
	case MACRO_ADMINPAGER:
	case MACRO_MAINCONFIGFILE:
	case MACRO_STATUSDATAFILE:
	case MACRO_RETENTIONDATAFILE:
	case MACRO_OBJECTCACHEFILE:
	case MACRO_TEMPFILE:
	case MACRO_LOGFILE:
	case MACRO_RESOURCEFILE:
	case MACRO_COMMANDFILE:
	case MACRO_HOSTPERFDATAFILE:
	case MACRO_SERVICEPERFDATAFILE:
	case MACRO_PROCESSSTARTTIME:
	case MACRO_TEMPPATH:
	case MACRO_EVENTSTARTTIME:

		/* no need to do any more work - these are already precomputed for us */
		*output = global_macros.x[macro_type];
		*free_macro = FALSE;
		break;

	/******************/
	/* SUMMARY MACROS */
	/******************/
	case MACRO_TOTALHOSTSUP:
	case MACRO_TOTALHOSTSDOWN:
	case MACRO_TOTALHOSTSUNREACHABLE:
	case MACRO_TOTALHOSTSDOWNUNHANDLED:
	case MACRO_TOTALHOSTSUNREACHABLEUNHANDLED:
	case MACRO_TOTALHOSTPROBLEMS:
	case MACRO_TOTALHOSTPROBLEMSUNHANDLED:
	case MACRO_TOTALSERVICESOK:
	case MACRO_TOTALSERVICESWARNING:
	case MACRO_TOTALSERVICESCRITICAL:
	case MACRO_TOTALSERVICESUNKNOWN:
	case MACRO_TOTALSERVICESWARNINGUNHANDLED:
	case MACRO_TOTALSERVICESCRITICALUNHANDLED:
	case MACRO_TOTALSERVICESUNKNOWNUNHANDLED:
	case MACRO_TOTALSERVICEPROBLEMS:
	case MACRO_TOTALSERVICEPROBLEMSUNHANDLED:

		/* generate summary macros if needed */
		if (mac->x[MACRO_TOTALHOSTSUP] == NULL) {

			/* get host totals */
			for (temp_host = host_list; temp_host != NULL; temp_host = temp_host->next) {

				/* filter totals based on contact if necessary */
				if (mac->contact_ptr != NULL)
					authorized = is_contact_for_host(temp_host, mac->contact_ptr);

				if (authorized == TRUE) {
					problem = TRUE;

					if (temp_host->current_state == STATE_UP && temp_host->has_been_checked == TRUE)
						hosts_up++;
					else if (temp_host->current_state == STATE_DOWN) {
						if (temp_host->scheduled_downtime_depth > 0)
							problem = FALSE;
						if (temp_host->problem_has_been_acknowledged == TRUE)
							problem = FALSE;
						if (temp_host->checks_enabled == FALSE)
							problem = FALSE;
						if (problem == TRUE)
							hosts_down_unhandled++;
						hosts_down++;
					} else if (temp_host->current_state == STATE_UNREACHABLE) {
						if (temp_host->scheduled_downtime_depth > 0)
							problem = FALSE;
						if (temp_host->problem_has_been_acknowledged == TRUE)
							problem = FALSE;
						if (temp_host->checks_enabled == FALSE)
							problem = FALSE;
						if (problem == TRUE)
							hosts_down_unhandled++;
						hosts_unreachable++;
					}
				}
			}

			host_problems = hosts_down + hosts_unreachable;
			host_problems_unhandled = hosts_down_unhandled + hosts_unreachable_unhandled;

			/* get service totals */
			for (temp_service = service_list; temp_service != NULL; temp_service = temp_service->next) {

				/* filter totals based on contact if necessary */
				if (mac->contact_ptr != NULL)
					authorized = is_contact_for_service(temp_service, mac->contact_ptr);

				if (authorized == TRUE) {
					problem = TRUE;

					if (temp_service->current_state == STATE_OK && temp_service->has_been_checked == TRUE)
						services_ok++;
					else if (temp_service->current_state == STATE_WARNING) {
						temp_host = find_host(temp_service->host_name);
						if (temp_host != NULL && (temp_host->current_state == STATE_DOWN || temp_host->current_state == STATE_UNREACHABLE))
							problem = FALSE;
						if (temp_service->scheduled_downtime_depth > 0)
							problem = FALSE;
						if (temp_service->problem_has_been_acknowledged == TRUE)
							problem = FALSE;
						if (temp_service->checks_enabled == FALSE)
							problem = FALSE;
						if (problem == TRUE)
							services_warning_unhandled++;
						services_warning++;
					} else if (temp_service->current_state == STATE_UNKNOWN) {
						temp_host = find_host(temp_service->host_name);
						if (temp_host != NULL && (temp_host->current_state == STATE_DOWN || temp_host->current_state == STATE_UNREACHABLE))
							problem = FALSE;
						if (temp_service->scheduled_downtime_depth > 0)
							problem = FALSE;
						if (temp_service->problem_has_been_acknowledged == TRUE)
							problem = FALSE;
						if (temp_service->checks_enabled == FALSE)
							problem = FALSE;
						if (problem == TRUE)
							services_unknown_unhandled++;
						services_unknown++;
					} else if (temp_service->current_state == STATE_CRITICAL) {
						temp_host = find_host(temp_service->host_name);
						if (temp_host != NULL && (temp_host->current_state == STATE_DOWN || temp_host->current_state == STATE_UNREACHABLE))
							problem = FALSE;
						if (temp_service->scheduled_downtime_depth > 0)
							problem = FALSE;
						if (temp_service->problem_has_been_acknowledged == TRUE)
							problem = FALSE;
						if (temp_service->checks_enabled == FALSE)
							problem = FALSE;
						if (problem == TRUE)
							services_critical_unhandled++;
						services_critical++;
					}
				}
			}

			service_problems = services_warning + services_critical + services_unknown;
			service_problems_unhandled = services_warning_unhandled + services_critical_unhandled + services_unknown_unhandled;

			/* these macros are time-intensive to compute, and will likely be used together, so save them all for future use */
			for (x = MACRO_TOTALHOSTSUP; x <= MACRO_TOTALSERVICEPROBLEMSUNHANDLED; x++)
				nm_free(mac->x[x]);
			nm_asprintf(&mac->x[MACRO_TOTALHOSTSUP], "%d", hosts_up);
			nm_asprintf(&mac->x[MACRO_TOTALHOSTSDOWN], "%d", hosts_down);
			nm_asprintf(&mac->x[MACRO_TOTALHOSTSUNREACHABLE], "%d", hosts_unreachable);
			nm_asprintf(&mac->x[MACRO_TOTALHOSTSDOWNUNHANDLED], "%d", hosts_down_unhandled);
			nm_asprintf(&mac->x[MACRO_TOTALHOSTSUNREACHABLEUNHANDLED], "%d", hosts_unreachable_unhandled);
			nm_asprintf(&mac->x[MACRO_TOTALHOSTPROBLEMS], "%d", host_problems);
			nm_asprintf(&mac->x[MACRO_TOTALHOSTPROBLEMSUNHANDLED], "%d", host_problems_unhandled);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICESOK], "%d", services_ok);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICESWARNING], "%d", services_warning);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICESCRITICAL], "%d", services_critical);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICESUNKNOWN], "%d", services_unknown);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICESWARNINGUNHANDLED], "%d", services_warning_unhandled);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICESCRITICALUNHANDLED], "%d", services_critical_unhandled);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICESUNKNOWNUNHANDLED], "%d", services_unknown_unhandled);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICEPROBLEMS], "%d", service_problems);
			nm_asprintf(&mac->x[MACRO_TOTALSERVICEPROBLEMSUNHANDLED], "%d", service_problems_unhandled);
		}

		/* return only the macro the user requested */
		*output = mac->x[macro_type];

		/* tell caller to NOT free memory when done */
		*free_macro = FALSE;
		break;

	default:
		log_debug_info(DEBUGL_MACROS, 0, "UNHANDLED MACRO #%d! THIS IS A BUG!\n", macro_type);
		return ERROR;
		break;
	}

	return result;
}


/* this is the big one */
static int grab_macro_value_r(nagios_macros *mac, char *macro_buffer, char **output, int *clean_options, int *free_macro)
{
	char *buf = NULL;
	char *ptr = NULL;
	char *macro_name = NULL;
	char *arg[2] = {NULL, NULL};
	contact *temp_contact = NULL;
	contactgroup *temp_contactgroup = NULL;
	contactsmember *temp_contactsmember = NULL;
	char *temp_buffer = NULL;
	int delimiter_len = 0;
	int x, result = OK;
	const struct macro_key_code *mkey;

	/* for the early cases, this is the default */
	*free_macro = FALSE;

	if (output == NULL)
		return ERROR;

	/* clear the old macro value */
	nm_free(*output);

	if (macro_buffer == NULL || free_macro == NULL)
		return ERROR;

	if (clean_options)
		*clean_options = 0;

	/*
	 * We handle argv and user macros first, since those are by far
	 * the most commonly accessed ones (3.4 and 1.005 per check,
	 * respectively). Since neither of them requires that we copy
	 * the original buffer, we can also get away with some less
	 * code for these simple cases.
	 */
	if (strstr(macro_buffer, "ARG") == macro_buffer) {

		/* which arg do we want? */
		x = atoi(macro_buffer + 3);

		if (x <= 0 || x > MAX_COMMAND_ARGUMENTS) {
			return ERROR;
		}

		/* use a pre-computed macro value */
		*output = mac->argv[x - 1];
		return OK;
	}

	if (strstr(macro_buffer, "USER") == macro_buffer) {

		/* which macro do we want? */
		x = atoi(macro_buffer + 4);

		if (x <= 0 || x > MAX_USER_MACROS) {
			return ERROR;
		}

		/* use a pre-computed macro value */
		*output = macro_user[x - 1];
		return OK;
	}

	if (strstr(macro_buffer, "VAULT") == macro_buffer) {
		return(broker_vault_macro(macro_buffer, output, free_macro, mac));
	}

	/* most frequently used "x" macro gets a shortcut */
	if (mac->host_ptr && !strcmp(macro_buffer, "HOSTADDRESS")) {
		if (mac->host_ptr->address)
			*output = mac->host_ptr->address;
		return OK;
	}

	/* work with a copy of the original buffer */
	buf = nm_strdup(macro_buffer);
	/* macro name is at start of buffer */
	macro_name = buf;

	/* see if there's an argument - if so, this is most likely an on-demand macro */
	if ((ptr = strchr(buf, ':'))) {

		ptr[0] = '\x0';
		ptr++;

		/* save the first argument - host name, hostgroup name, etc. */
		arg[0] = ptr;

		/* try and find a second argument */
		if ((ptr = strchr(ptr, ':'))) {

			ptr[0] = '\x0';
			ptr++;

			/* save second argument - service description or delimiter */
			arg[1] = ptr;
		}
	}

	if ((mkey = find_macro_key(macro_name))) {
		log_debug_info(DEBUGL_MACROS, 2, "  macros[%d] (%s) match.\n", mkey->code, macro_x_names[mkey->code]);

		/* get the macro value */
		result = grab_macrox_value_r(mac, mkey->code, arg[0], arg[1], output, free_macro);

		/* Return the macro attributes */

		if (clean_options) {
			*clean_options = mkey->options;
		}
	}
	/***** CONTACT ADDRESS MACROS *****/
	/* NOTE: the code below should be broken out into a separate function */
	else if (strstr(macro_name, "CONTACTADDRESS") == macro_name) {

		/* which address do we want? */
		x = atoi(macro_name + 14) - 1;

		/* regular macro */
		if (arg[0] == NULL) {

			/* use the saved pointer */
			if ((temp_contact = mac->contact_ptr) == NULL) {
				nm_free(buf);
				return ERROR;
			}

			/* get the macro value by reference, so no need to free() */
			*free_macro = FALSE;
			result = grab_contact_address_macro(x, temp_contact, output);
		}

		/* on-demand macro */
		else {

			/* on-demand contact macro with a contactgroup and a delimiter */
			if (arg[1] != NULL) {

				if ((temp_contactgroup = find_contactgroup(arg[0])) == NULL)
					return ERROR;

				delimiter_len = strlen(arg[1]);

				/* concatenate macro values for all contactgroup members */
				for (temp_contactsmember = temp_contactgroup->members; temp_contactsmember != NULL; temp_contactsmember = temp_contactsmember->next) {

					if ((temp_contact = temp_contactsmember->contact_ptr) == NULL)
						continue;

					/* get the macro value for this contact */
					grab_contact_address_macro(x, temp_contact, &temp_buffer);

					if (temp_buffer == NULL)
						continue;

					/* add macro value to already running macro */
					if (*output == NULL)
						*output = nm_strdup(temp_buffer);
					else {
						*output = nm_realloc(*output, strlen(*output) + strlen(temp_buffer) + delimiter_len + 1);
						strcat(*output, arg[1]);
						strcat(*output, temp_buffer);
					}
					nm_free(temp_buffer);
				}
			}

			/* else on-demand contact macro */
			else {

				/* find the contact */
				if ((temp_contact = find_contact(arg[0])) == NULL) {
					nm_free(buf);
					return ERROR;
				}

				/* get the macro value */
				result = grab_contact_address_macro(x, temp_contact, output);
			}
		}
	}

	/***** CUSTOM VARIABLE MACROS *****/
	else if (macro_name[0] == '_') {

		/* get the macro value */
		result = grab_custom_macro_value_r(mac, macro_name, arg[0], arg[1], output);
	}

	/* no macro matched... */
	else {
		log_debug_info(DEBUGL_MACROS, 0, " WARNING: Could not find a macro matching '%s'!\n", macro_name);
		result = ERROR;
	}

	nm_free(buf);

	return result;
}


/*
 * replace macros in notification commands with their values,
 * the thread-safe version
 */
int process_macros_r(nagios_macros *mac, char *input_buffer, char **output_buffer, int options)
{
	char *temp_buffer = NULL;
	char *save_buffer = NULL;
	char *buf_ptr = NULL;
	char *delim_ptr = NULL;
	int in_macro = FALSE;
	char *selected_macro = NULL;
	char *original_macro = NULL;
	int result = OK;
	int free_macro = FALSE;
	int macro_options = 0;

	if (output_buffer == NULL || input_buffer == NULL)
		return ERROR;

	*output_buffer = nm_strdup("");
	in_macro = FALSE;

	log_debug_info(DEBUGL_MACROS, 1, "**** BEGIN MACRO PROCESSING ***********\n");
	log_debug_info(DEBUGL_MACROS, 1, "Processing: '%s'\n", input_buffer);

	/* use a duplicate of original buffer, so we don't modify the original */
	save_buffer = buf_ptr = nm_strdup(input_buffer);
	while (buf_ptr) {

		/* save pointer to this working part of buffer */
		temp_buffer = buf_ptr;

		/* find the next delimiter - terminate preceding string and advance buffer pointer for next run */
		if ((delim_ptr = strchr(buf_ptr, '$'))) {
			delim_ptr[0] = '\x0';
			buf_ptr = (char *)delim_ptr + 1;
		}
		/* no delimiter found - we already have the last of the buffer */
		else
			buf_ptr = NULL;

		log_debug_info(DEBUGL_MACROS, 2, "  Processing part: '%s'\n", temp_buffer);

		/* we're in plain text... */
		if (in_macro == FALSE) {

			/* add the plain text to the end of the already processed buffer */
			*output_buffer = nm_realloc(*output_buffer, strlen(*output_buffer) + strlen(temp_buffer) + 1);
			strcat(*output_buffer, temp_buffer);

			log_debug_info(DEBUGL_MACROS, 2, "  Not currently in macro.  Running output (%lu): '%s'\n", (unsigned long)strlen(*output_buffer), *output_buffer);
			in_macro = TRUE;
			continue;
		}

		/* an escaped $ is done by specifying two $$ next to each other */
		if (!strcmp(temp_buffer, "")) {
			log_debug_info(DEBUGL_MACROS, 2, "  Escaped $.  Running output (%lu): '%s'\n", (unsigned long)strlen(*output_buffer), *output_buffer);
			*output_buffer = nm_realloc(*output_buffer, strlen(*output_buffer) + 2);
			strcat(*output_buffer, "$");
			in_macro = FALSE;
			continue;
		}

		/* looks like we're in a macro, so process it... */
		/* grab the macro value */
		free_macro = FALSE;
		selected_macro = NULL;
		result = grab_macro_value_r(mac, temp_buffer, &selected_macro, &macro_options, &free_macro);
		log_debug_info(DEBUGL_MACROS, 2, "  Processed '%s', Free: %d\n", temp_buffer, free_macro);

		/**
		 * we couldn't parse the macro cause the macro
		 * doesn't exist, so continue on
		 */
		if (result != OK) {
			if (free_macro == TRUE)
				nm_free(selected_macro);

			/* add the plain text to the end of the already processed buffer */
			*output_buffer = nm_realloc(*output_buffer, strlen(*output_buffer) + strlen(temp_buffer) + 3);
			strcat(*output_buffer, "$");
			strcat(*output_buffer, temp_buffer);

			/* if we still do not reach the end of string */
			if (buf_ptr)
				strcat(*output_buffer, "$");

			in_macro = FALSE;
			continue;
		}

		/* insert macro */
		if (selected_macro != NULL) {
			log_debug_info(DEBUGL_MACROS, 2, "  Processed '%s', Free: %d,  Cleaning options: %d\n", temp_buffer, free_macro, options);

			/* URL encode the macro if requested - this allocates new memory */
			if (options & URL_ENCODE_MACRO_CHARS) {
				original_macro = selected_macro;
				selected_macro = get_url_encoded_string(selected_macro);
				if (free_macro == TRUE) {
					nm_free(original_macro);
				}
				free_macro = TRUE;
			}

			/* some macros should sometimes be cleaned */
			if (macro_options & options & (STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS)) {
				char *cleaned_macro = NULL;

				/* add the (cleaned) processed macro to the end of the already processed buffer */
				if (selected_macro != NULL && (cleaned_macro = clean_macro_chars(selected_macro, options)) != NULL) {
					*output_buffer = nm_realloc(*output_buffer, strlen(*output_buffer) + strlen(cleaned_macro) + 1);
					strcat(*output_buffer, cleaned_macro);
					if (*cleaned_macro)
						free(cleaned_macro);

					log_debug_info(DEBUGL_MACROS, 2, "  Cleaned macro.  Running output (%lu): '%s'\n", (unsigned long)strlen(*output_buffer), *output_buffer);
				}
			}

			/* others are not cleaned */
			else {
				/* add the processed macro to the end of the already processed buffer */
				if (selected_macro != NULL) {
					*output_buffer = nm_realloc(*output_buffer, strlen(*output_buffer) + strlen(selected_macro) + 1);
					strcat(*output_buffer, selected_macro);

					log_debug_info(DEBUGL_MACROS, 2, "  Uncleaned macro.  Running output (%lu): '%s'\n", (unsigned long)strlen(*output_buffer), *output_buffer);
				}
			}

			/* free memory if necessary (if we URL encoded the macro or we were told to do so by grab_macro_value()) */
			if (free_macro == TRUE)
				nm_free(selected_macro);

			log_debug_info(DEBUGL_MACROS, 2, "  Just finished macro.  Running output (%lu): '%s'\n", (unsigned long)strlen(*output_buffer), *output_buffer);
		}

		in_macro = FALSE;
	}

	/* free copy of input buffer */
	nm_free(save_buffer);

	log_debug_info(DEBUGL_MACROS, 1, "  Done.  Final output: '%s'\n", *output_buffer);
	log_debug_info(DEBUGL_MACROS, 1, "**** END MACRO PROCESSING *************\n");

	return OK;
}

int process_macros(char *input_buffer, char **output_buffer, int options)
{
	return process_macros_r(&global_macros, input_buffer, output_buffer, options);
}


/******************************************************************/
/***************** MACRO INITIALIZATION FUNCTIONS *****************/
/******************************************************************/

static int macro_key_cmp(const void *a_, const void *b_)
{
	struct macro_key_code *a = (struct macro_key_code *)a_;
	struct macro_key_code *b = (struct macro_key_code *)b_;

	return strcmp(a->name, b->name);
}

/* initializes global macros */
int init_macros(void)
{
	int x;
	init_macrox_names();

	for (x = 0; x < 32; x++)
		illegal_output_char_map[x] = 1;
	illegal_output_char_map[127] = 1;

	/*
	 * non-volatile macros are free()'d when they're set.
	 * We must do this in order to not lose the constant
	 * ones when we get SIGHUP or a RESTART_PROGRAM event
	 * from the command fifo. Otherwise a memset() would
	 * have been better.
	 */
	clear_volatile_macros_r(&global_macros);

	/* backwards compatibility hack */
	macro_x = global_macros.x;

	/*
	 * Now build an ordered list of X macro names so we can
	 * do binary lookups later and avoid a ton of strcmp()'s
	 * for each and every check that gets run. A hash table
	 * is actually slower, since the most frequently used
	 * keys are so long and a binary lookup is completed in
	 * 7 steps for up to ~200 keys, worst case.
	 */
	for (x = 0; x < MACRO_X_COUNT; x++) {
		macro_keys[x].code = x;
		macro_keys[x].name = macro_x_names[x];

		/* This tells which escaping is possible to do on the macro */
		macro_keys[x].options = URL_ENCODE_MACRO_CHARS;
		switch (x) {
		case MACRO_HOSTOUTPUT:
		case MACRO_LONGHOSTOUTPUT:
		case MACRO_HOSTPERFDATA:
		case MACRO_HOSTACKAUTHOR:
		case MACRO_HOSTACKCOMMENT:
		case MACRO_SERVICEOUTPUT:
		case MACRO_LONGSERVICEOUTPUT:
		case MACRO_SERVICEPERFDATA:
		case MACRO_SERVICEACKAUTHOR:
		case MACRO_SERVICEACKCOMMENT:
		case MACRO_HOSTCHECKCOMMAND:
		case MACRO_SERVICECHECKCOMMAND:
		case MACRO_HOSTNOTES:
		case MACRO_SERVICENOTES:
		case MACRO_HOSTGROUPNOTES:
		case MACRO_SERVICEGROUPNOTES:
			macro_keys[x].options |= STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS;
			break;
		}
	}

	qsort(macro_keys, x, sizeof(struct macro_key_code), macro_key_cmp);
	return OK;
}

/*
 * initializes the names of macros, using this nifty little macro
 * which ensures we never add any typos to the list
 */
#define add_macrox_name(name) macro_x_names[MACRO_##name] = nm_strdup(#name)
int init_macrox_names(void)
{
	register int x = 0;

	/* initialize macro names */
	for (x = 0; x < MACRO_X_COUNT; x++)
		macro_x_names[x] = NULL;

	/* initialize each macro name */
	add_macrox_name(HOSTNAME);
	add_macrox_name(HOSTALIAS);
	add_macrox_name(HOSTADDRESS);
	add_macrox_name(SERVICEDESC);
	add_macrox_name(SERVICESTATE);
	add_macrox_name(SERVICESTATEID);
	add_macrox_name(SERVICEATTEMPT);
	add_macrox_name(SERVICEISVOLATILE);
	add_macrox_name(LONGDATETIME);
	add_macrox_name(SHORTDATETIME);
	add_macrox_name(DATE);
	add_macrox_name(TIME);
	add_macrox_name(TIMET);
	add_macrox_name(LASTHOSTCHECK);
	add_macrox_name(LASTSERVICECHECK);
	add_macrox_name(LASTHOSTSTATECHANGE);
	add_macrox_name(LASTSERVICESTATECHANGE);
	add_macrox_name(HOSTOUTPUT);
	add_macrox_name(SERVICEOUTPUT);
	add_macrox_name(HOSTPERFDATA);
	add_macrox_name(SERVICEPERFDATA);
	add_macrox_name(CONTACTNAME);
	add_macrox_name(CONTACTALIAS);
	add_macrox_name(CONTACTEMAIL);
	add_macrox_name(CONTACTPAGER);
	add_macrox_name(ADMINEMAIL);
	add_macrox_name(ADMINPAGER);
	add_macrox_name(HOSTSTATE);
	add_macrox_name(HOSTSTATEID);
	add_macrox_name(HOSTATTEMPT);
	add_macrox_name(NOTIFICATIONTYPE);
	add_macrox_name(NOTIFICATIONNUMBER);
	add_macrox_name(NOTIFICATIONISESCALATED);
	add_macrox_name(HOSTEXECUTIONTIME);
	add_macrox_name(SERVICEEXECUTIONTIME);
	add_macrox_name(HOSTLATENCY);
	add_macrox_name(SERVICELATENCY);
	add_macrox_name(HOSTDURATION);
	add_macrox_name(SERVICEDURATION);
	add_macrox_name(HOSTDURATIONSEC);
	add_macrox_name(SERVICEDURATIONSEC);
	add_macrox_name(HOSTDOWNTIME);
	add_macrox_name(SERVICEDOWNTIME);
	add_macrox_name(HOSTSTATETYPE);
	add_macrox_name(SERVICESTATETYPE);
	add_macrox_name(HOSTPERCENTCHANGE);
	add_macrox_name(SERVICEPERCENTCHANGE);
	add_macrox_name(HOSTGROUPNAME);
	add_macrox_name(HOSTGROUPALIAS);
	add_macrox_name(SERVICEGROUPNAME);
	add_macrox_name(SERVICEGROUPALIAS);
	add_macrox_name(HOSTACKAUTHOR);
	add_macrox_name(HOSTACKCOMMENT);
	add_macrox_name(SERVICEACKAUTHOR);
	add_macrox_name(SERVICEACKCOMMENT);
	add_macrox_name(LASTSERVICEOK);
	add_macrox_name(LASTSERVICEWARNING);
	add_macrox_name(LASTSERVICEUNKNOWN);
	add_macrox_name(LASTSERVICECRITICAL);
	add_macrox_name(LASTHOSTUP);
	add_macrox_name(LASTHOSTDOWN);
	add_macrox_name(LASTHOSTUNREACHABLE);
	add_macrox_name(SERVICECHECKCOMMAND);
	add_macrox_name(HOSTCHECKCOMMAND);
	add_macrox_name(MAINCONFIGFILE);
	add_macrox_name(STATUSDATAFILE);
	add_macrox_name(HOSTDISPLAYNAME);
	add_macrox_name(SERVICEDISPLAYNAME);
	add_macrox_name(RETENTIONDATAFILE);
	add_macrox_name(OBJECTCACHEFILE);
	add_macrox_name(TEMPFILE);
	add_macrox_name(LOGFILE);
	add_macrox_name(RESOURCEFILE);
	add_macrox_name(COMMANDFILE);
	add_macrox_name(HOSTPERFDATAFILE);
	add_macrox_name(SERVICEPERFDATAFILE);
	add_macrox_name(HOSTACTIONURL);
	add_macrox_name(HOSTNOTESURL);
	add_macrox_name(HOSTNOTES);
	add_macrox_name(SERVICEACTIONURL);
	add_macrox_name(SERVICENOTESURL);
	add_macrox_name(SERVICENOTES);
	add_macrox_name(TOTALHOSTSUP);
	add_macrox_name(TOTALHOSTSDOWN);
	add_macrox_name(TOTALHOSTSUNREACHABLE);
	add_macrox_name(TOTALHOSTSDOWNUNHANDLED);
	add_macrox_name(TOTALHOSTSUNREACHABLEUNHANDLED);
	add_macrox_name(TOTALHOSTPROBLEMS);
	add_macrox_name(TOTALHOSTPROBLEMSUNHANDLED);
	add_macrox_name(TOTALSERVICESOK);
	add_macrox_name(TOTALSERVICESWARNING);
	add_macrox_name(TOTALSERVICESCRITICAL);
	add_macrox_name(TOTALSERVICESUNKNOWN);
	add_macrox_name(TOTALSERVICESWARNINGUNHANDLED);
	add_macrox_name(TOTALSERVICESCRITICALUNHANDLED);
	add_macrox_name(TOTALSERVICESUNKNOWNUNHANDLED);
	add_macrox_name(TOTALSERVICEPROBLEMS);
	add_macrox_name(TOTALSERVICEPROBLEMSUNHANDLED);
	add_macrox_name(PROCESSSTARTTIME);
	add_macrox_name(HOSTCHECKTYPE);
	add_macrox_name(SERVICECHECKTYPE);
	add_macrox_name(LONGHOSTOUTPUT);
	add_macrox_name(LONGSERVICEOUTPUT);
	add_macrox_name(TEMPPATH);
	add_macrox_name(HOSTNOTIFICATIONNUMBER);
	add_macrox_name(SERVICENOTIFICATIONNUMBER);
	add_macrox_name(HOSTNOTIFICATIONID);
	add_macrox_name(SERVICENOTIFICATIONID);
	add_macrox_name(HOSTEVENTID);
	add_macrox_name(LASTHOSTEVENTID);
	add_macrox_name(SERVICEEVENTID);
	add_macrox_name(LASTSERVICEEVENTID);
	add_macrox_name(HOSTGROUPNAMES);
	add_macrox_name(SERVICEGROUPNAMES);
	add_macrox_name(HOSTACKAUTHORNAME);
	add_macrox_name(HOSTACKAUTHORALIAS);
	add_macrox_name(SERVICEACKAUTHORNAME);
	add_macrox_name(SERVICEACKAUTHORALIAS);
	add_macrox_name(MAXHOSTATTEMPTS);
	add_macrox_name(MAXSERVICEATTEMPTS);
	add_macrox_name(TOTALHOSTSERVICES);
	add_macrox_name(TOTALHOSTSERVICESOK);
	add_macrox_name(TOTALHOSTSERVICESWARNING);
	add_macrox_name(TOTALHOSTSERVICESUNKNOWN);
	add_macrox_name(TOTALHOSTSERVICESCRITICAL);
	add_macrox_name(HOSTGROUPNOTES);
	add_macrox_name(HOSTGROUPNOTESURL);
	add_macrox_name(HOSTGROUPACTIONURL);
	add_macrox_name(SERVICEGROUPNOTES);
	add_macrox_name(SERVICEGROUPNOTESURL);
	add_macrox_name(SERVICEGROUPACTIONURL);
	add_macrox_name(HOSTGROUPMEMBERS);
	add_macrox_name(SERVICEGROUPMEMBERS);
	add_macrox_name(CONTACTGROUPNAME);
	add_macrox_name(CONTACTGROUPALIAS);
	add_macrox_name(CONTACTGROUPMEMBERS);
	add_macrox_name(CONTACTGROUPNAMES);
	add_macrox_name(NOTIFICATIONRECIPIENTS);
	add_macrox_name(NOTIFICATIONAUTHOR);
	add_macrox_name(NOTIFICATIONAUTHORNAME);
	add_macrox_name(NOTIFICATIONAUTHORALIAS);
	add_macrox_name(NOTIFICATIONCOMMENT);
	add_macrox_name(EVENTSTARTTIME);
	add_macrox_name(HOSTPROBLEMID);
	add_macrox_name(LASTHOSTPROBLEMID);
	add_macrox_name(SERVICEPROBLEMID);
	add_macrox_name(LASTSERVICEPROBLEMID);
	add_macrox_name(ISVALIDTIME);
	add_macrox_name(NEXTVALIDTIME);
	add_macrox_name(LASTHOSTSTATE);
	add_macrox_name(LASTHOSTSTATEID);
	add_macrox_name(LASTSERVICESTATE);
	add_macrox_name(LASTSERVICESTATEID);
	add_macrox_name(HOSTVALUE);
	add_macrox_name(SERVICEVALUE);
	add_macrox_name(PROBLEMVALUE);
	add_macrox_name(HOSTPROBLEMSTART);
	add_macrox_name(HOSTPROBLEMEND);
	add_macrox_name(HOSTPROBLEMDURATIONSEC);
	add_macrox_name(HOSTPROBLEMDURATION);
	add_macrox_name(SERVICEPROBLEMSTART);
	add_macrox_name(SERVICEPROBLEMEND);
	add_macrox_name(SERVICEPROBLEMDURATIONSEC);
	add_macrox_name(SERVICEPROBLEMDURATION);

	return OK;
}


/******************************************************************/
/********************* MACRO CLEANUP FUNCTIONS ********************/
/******************************************************************/

/* free memory associated with the macrox names */
int free_macrox_names(void)
{
	register int x = 0;

	/* free each macro name */
	for (x = 0; x < MACRO_X_COUNT; x++)
		nm_free(macro_x_names[x]);

	return OK;
}



/* clear argv macros - used in commands */
int clear_argv_macros_r(nagios_macros *mac)
{
	register int x = 0;

	/* command argument macros */
	for (x = 0; x < MAX_COMMAND_ARGUMENTS; x++)
		nm_free(mac->argv[x]);

	return OK;
}


/* clear all macros that are not "constant" (i.e. they change throughout the course of monitoring) */
int clear_volatile_macros_r(nagios_macros *mac)
{
	customvariablesmember *this_customvariablesmember = NULL;
	customvariablesmember *next_customvariablesmember = NULL;
	register int x = 0;

	for (x = 0; x < MACRO_X_COUNT; x++) {
		switch (x) {

		case MACRO_ADMINEMAIL:
		case MACRO_ADMINPAGER:
		case MACRO_MAINCONFIGFILE:
		case MACRO_STATUSDATAFILE:
		case MACRO_RETENTIONDATAFILE:
		case MACRO_OBJECTCACHEFILE:
		case MACRO_TEMPFILE:
		case MACRO_LOGFILE:
		case MACRO_RESOURCEFILE:
		case MACRO_COMMANDFILE:
		case MACRO_HOSTPERFDATAFILE:
		case MACRO_SERVICEPERFDATAFILE:
		case MACRO_PROCESSSTARTTIME:
		case MACRO_TEMPPATH:
		case MACRO_EVENTSTARTTIME:
		case MACRO_TOTALHOSTSERVICES:
		case MACRO_TOTALHOSTSERVICESOK:
		case MACRO_TOTALHOSTSERVICESWARNING:
		case MACRO_TOTALHOSTSERVICESUNKNOWN:
		case MACRO_TOTALHOSTSERVICESCRITICAL:
			/* these don't change during the course of monitoring, so no need to free them */
			break;
		default:
			nm_free(mac->x[x]);
			break;
		}
	}

	/* contact address macros */
	for (x = 0; x < MAX_CONTACT_ADDRESSES; x++)
		nm_free(mac->contactaddress[x]);

	/* clear macro pointers */
	mac->host_ptr = NULL;
	mac->hostgroup_ptr = NULL;
	mac->service_ptr = NULL;
	mac->servicegroup_ptr = NULL;
	mac->contact_ptr = NULL;
	mac->contactgroup_ptr = NULL;

	/* clear on-demand macro */
	nm_free(mac->ondemand);

	/* clear ARGx macros */
	clear_argv_macros_r(mac);

	/* clear custom host variables */
	for (this_customvariablesmember = mac->custom_host_vars; this_customvariablesmember != NULL; this_customvariablesmember = next_customvariablesmember) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
	}
	mac->custom_host_vars = NULL;

	/* clear custom service variables */
	for (this_customvariablesmember = mac->custom_service_vars; this_customvariablesmember != NULL; this_customvariablesmember = next_customvariablesmember) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
	}
	mac->custom_service_vars = NULL;

	/* clear custom contact variables */
	for (this_customvariablesmember = mac->custom_contact_vars; this_customvariablesmember != NULL; this_customvariablesmember = next_customvariablesmember) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
	}
	mac->custom_contact_vars = NULL;

	return OK;
}


/* clear service macros */
int clear_service_macros_r(nagios_macros *mac)
{
	customvariablesmember *this_customvariablesmember = NULL;
	customvariablesmember *next_customvariablesmember = NULL;

	/* these are recursive but persistent. what to do? */
	nm_free(mac->x[MACRO_SERVICECHECKCOMMAND]);
	nm_free(mac->x[MACRO_SERVICEACTIONURL]);
	nm_free(mac->x[MACRO_SERVICENOTESURL]);
	nm_free(mac->x[MACRO_SERVICENOTES]);

	nm_free(mac->x[MACRO_SERVICEGROUPNAMES]);

	/* clear custom service variables */
	for (this_customvariablesmember = mac->custom_service_vars; this_customvariablesmember != NULL; this_customvariablesmember = next_customvariablesmember) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
	}
	mac->custom_service_vars = NULL;

	/* clear pointers */
	mac->service_ptr = NULL;

	return OK;
}


/* clear host macros */
int clear_host_macros_r(nagios_macros *mac)
{
	customvariablesmember *this_customvariablesmember = NULL;
	customvariablesmember *next_customvariablesmember = NULL;

	/* these are recursive but persistent. what to do? */
	nm_free(mac->x[MACRO_HOSTCHECKCOMMAND]);
	nm_free(mac->x[MACRO_HOSTACTIONURL]);
	nm_free(mac->x[MACRO_HOSTNOTESURL]);
	nm_free(mac->x[MACRO_HOSTNOTES]);

	/* numbers or by necessity autogenerated strings */
	nm_free(mac->x[MACRO_HOSTGROUPNAMES]);

	/* clear custom host variables */
	for (this_customvariablesmember = mac->custom_host_vars; this_customvariablesmember != NULL; this_customvariablesmember = next_customvariablesmember) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
	}
	mac->custom_host_vars = NULL;

	/* clear pointers */
	mac->host_ptr = NULL;

	return OK;
}


/* clear hostgroup macros */
int clear_hostgroup_macros_r(nagios_macros *mac)
{

	/* recursive but persistent. what to do? */
	nm_free(mac->x[MACRO_HOSTGROUPACTIONURL]);
	nm_free(mac->x[MACRO_HOSTGROUPNOTESURL]);
	nm_free(mac->x[MACRO_HOSTGROUPNOTES]);

	/* generated */
	nm_free(mac->x[MACRO_HOSTGROUPMEMBERS]);

	/* clear pointers */
	mac->hostgroup_ptr = NULL;

	return OK;
}


/* clear servicegroup macros */
int clear_servicegroup_macros_r(nagios_macros *mac)
{
	/* recursive but persistent. what to do? */
	nm_free(mac->x[MACRO_SERVICEGROUPACTIONURL]);
	nm_free(mac->x[MACRO_SERVICEGROUPNOTESURL]);
	nm_free(mac->x[MACRO_SERVICEGROUPNOTES]);

	/* generated */
	nm_free(mac->x[MACRO_SERVICEGROUPMEMBERS]);

	/* clear pointers */
	mac->servicegroup_ptr = NULL;

	return OK;
}


/* clear contact macros */
int clear_contact_macros_r(nagios_macros *mac)
{
	customvariablesmember *this_customvariablesmember = NULL;
	customvariablesmember *next_customvariablesmember = NULL;

	/* generated */
	nm_free(mac->x[MACRO_CONTACTGROUPNAMES]);

	/* clear custom contact variables */
	for (this_customvariablesmember = mac->custom_contact_vars; this_customvariablesmember != NULL; this_customvariablesmember = next_customvariablesmember) {
		next_customvariablesmember = this_customvariablesmember->next;
		nm_free(this_customvariablesmember->variable_name);
		nm_free(this_customvariablesmember->variable_value);
		nm_free(this_customvariablesmember);
	}
	mac->custom_contact_vars = NULL;

	/* clear pointers */
	mac->contact_ptr = NULL;

	return OK;
}


/* clear contactgroup macros */
int clear_contactgroup_macros_r(nagios_macros *mac)
{
	/* generated */
	nm_free(mac->x[MACRO_CONTACTGROUPMEMBERS]);

	/* clear pointers */
	mac->contactgroup_ptr = NULL;

	return OK;
}


/* clear summary macros */
int clear_summary_macros_r(nagios_macros *mac)
{
	register int x;

	for (x = MACRO_TOTALHOSTSUP; x <= MACRO_TOTALSERVICEPROBLEMSUNHANDLED; x++)
		nm_free(mac->x[x]);

	return OK;
}
