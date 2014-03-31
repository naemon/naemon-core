/*****************************************************************************
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
*****************************************************************************/
#include <string.h>
#include <assert.h>
#include "tap.h"
#include "naemon/objects.h"
#include "naemon/commands.h"
#include "naemon/downtime.h"
#include "naemon/comments.h"
#include "naemon/globals.h"
#include "naemon/utils.h"
#include "naemon/configuration.h"
#include "naemon/defaults.h"
#include "naemon/sretention.h"
#include "naemon/events.h"

static int *received_persistent;
static char *received_host;
static time_t received_entry_time;
char *addresses[2];
timeperiod *registered_timeperiod = NULL;
int b_val;
int i_val;
time_t t_val;
int error = 0;
char * s_val;


int test__add_host_comment_handler (const struct external_command *ext_command, time_t entry_time)
{
	command_argument_value_copy((void**) &received_host, command_argument_get_value(ext_command, "host"), STRING);
	command_argument_value_copy((void**) &received_persistent, command_argument_get_value(ext_command, "persistent"), BOOL);
	received_entry_time = entry_time;
	return 0;
}
int test__add_service_comment_handler(const struct external_command *ext_command, time_t entry_time)
{
	return 0;
}

int test__del_host_comment_handler(const struct external_command *ext_command,time_t entry_time)
{
	return 0;
}

int test__disable_notifications_handler(const struct external_command *ext_command, time_t entry_time)
{
	return 0;
}

int test__do_thing_with_timeperiod_handler(const struct external_command *ext_command, time_t entry_time)
{
	return 0;
}


int test__do_thing_with_contact_handler(const struct external_command *ext_command, time_t entry_time)
{
	return 0;
}
int custom_service_validator(void *value)
{
	return 1;
}

void test_register(void)
{
	struct external_command *ext_command = NULL;
	int author = 42, persistent = 0;
	char command_name[21];
	int expected_command_index = 0;
	registered_commands_init(20);
	while ( expected_command_index  < 60 ) { /*Verify that auto-growing the register works*/
		(void)snprintf(command_name, 21, "ADD_HOST_COMMENT_%d", expected_command_index+1);
		ext_command = command_create(command_name, test__add_host_comment_handler, "This is a description for a command named ADD_HOST_COMMENT", NULL);
		command_argument_add(ext_command, "host", STRING, NULL, NULL);
		b_val = 0;
		command_argument_add(ext_command, "persistent", BOOL, &b_val, NULL);
		i_val = 42;
		command_argument_add(ext_command, "author", INTEGER, &i_val, NULL);
		s_val = "No comment";
		command_argument_add(ext_command, "comment", STRING, s_val, NULL);

		ok(expected_command_index == command_register(ext_command, -1), "command registration is successful");
		ok((NULL == command_argument_get_value(ext_command, "host")), "Host (null) default value saved properly");
		ok(persistent == *(int *)command_argument_get_value(ext_command, "persistent"), "Persistent (bool) default value saved properly");
		ok(author == *(int *)command_argument_get_value(ext_command, "author"), "Author (int) default value saved properly");
		ok(!strcmp("No comment", command_argument_get_value(ext_command, "comment")), "Comment (str) default value saved properly");
		++expected_command_index;
	}
	registered_commands_deinit();
}

void test_parsing(void)
{
	struct external_command *ext_command = NULL;
	contact *created_contact = NULL;
	contact *fetched_contact = NULL;
	const char *cmdstr = "[1234567890] ADD_HOST_COMMENT;my_host;0;15;this is my comment, there are many like it but this one is mine";
	registered_commands_init(20);
	{
		ok(NULL == command_parse(cmdstr, COMMAND_SYNTAX_NOKV, &error), "We can't parse commands when none are registered");
		ok(CMD_ERROR_UNKNOWN_COMMAND == error, "The error code looks like expected");

		ext_command = command_create("ADD_HOST_COMMENT", test__add_host_comment_handler, "This is a description for a command named ADD_HOST_COMMENT", NULL);
		command_argument_add(ext_command, "host", STRING, NULL, NULL);
		b_val = 0;
		command_argument_add(ext_command, "persistent", BOOL, &b_val, NULL);
		i_val = 42;
		command_argument_add(ext_command, "author", INTEGER, &i_val, NULL);
		s_val = "No comment";
		command_argument_add(ext_command, "comment", STRING, s_val, NULL);
		command_register(ext_command, -1);

		ext_command = command_parse("[] UNKNOWN_COMMAND", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_MALFORMED_COMMAND == error, "Malformed command error is raised for malformed commands");
		ok(NULL == ext_command, "No command returned for malformed command");

		ext_command = command_parse("[UNKNOWN_COMMAND", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_MALFORMED_COMMAND == error, "Malformed command error is raised for malformed commands");
		ok(NULL == ext_command, "No command returned for malformed command");

		ext_command = command_parse("[139414354] UNKNOWN_COMMAND", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_UNKNOWN_COMMAND == error, "Unknown command error is raised for unknown commands");
		ok(NULL == ext_command, "No command returned for unknown command");

		ext_command = command_parse(cmdstr, COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_OK == error, "The command parses without error");
		ok(!strcmp("my_host", command_argument_get_value(ext_command, "host")), "Host value parsed successfully");
		ok(0 == *(int *)command_argument_get_value(ext_command, "persistent"), "Persistent value parsed successfully");
		ok(15 == *(int *)command_argument_get_value(ext_command, "author"), "Author value parsed successfully");
		ok(!strcmp("this is my comment, there are many like it but this one is mine", command_argument_get_value(ext_command, "comment")), "Comment value parsed successfully");

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT;my_host;0;15;this is my newline\n, there are many like it but this one is\n m\ni\nn\ne", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_OK == error, "Command containing newlines parses without error");
		ok(!strcmp("this is my newline\n, there are many like it but this one is\n m\ni\nn\ne", command_argument_get_value(ext_command, "comment")), "Comment containing newlines parsed successfully");

		ext_command = command_parse(cmdstr, COMMAND_SYNTAX_NOKV, &error);
		ok(0 == command_execute_handler(ext_command), "Callback exit value properly passed on");
		ok(!strcmp("my_host", received_host), "Host value passed to callback");
		ok(0 == *received_persistent, "Persistent value passed to callback");
		ok(received_entry_time == 1234567890, "Entry time passed correctly to callback");
		command_destroy(ext_command);
		free(received_host);
		free(received_persistent);

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT;;1", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_PARSE_MISSING_ARG == error, "Missing arguments are complained about");
		ok(ext_command == NULL, "No command returned for command with missing arguments");

		ext_command = command_parse("[15341345] ADD_HOST_COMMENT", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_PARSE_MISSING_ARG == error, "Missing arguments are complained about (no arguments supplied)");
		ok(ext_command == NULL, "No command returned for command with missing arguments");

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT;my_host;0;441;this is my comment, there are many like it but this one is mine;Post-semi-colon stuff", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_OK == error, "Last string argument may contain semi-colons");
		ok(ext_command != NULL, "A command should be returned when last string-argument has semi-colons");

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT;my_host;0;Dora the Explora';this is my comment, there are many like it but this one is mine", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_PARSE_TYPE_MISMATCH == error, "Type errors are complained about");
		ok(ext_command == NULL, "No command returned for command with argument type errors");

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT;my_host;1;4lyfe;this is my comment, there are many like it but this one is mine", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_PARSE_TYPE_MISMATCH == error, "Junk characters after integer arguments are complained about");
		ok(ext_command == NULL, "No command returned for command with argument type mismatch errors");

		ext_command = command_create("ADD_HOST_COMMENT_WITH_TIMESTAMP", test__add_host_comment_handler, "This is a description for a command named ADD_HOST_COMMENT", NULL);
		command_argument_add(ext_command, "host", STRING, NULL, NULL);
		command_argument_add(ext_command, "persistent", BOOL, NULL, NULL);
		i_val = 42;
		command_argument_add(ext_command, "author", INTEGER, &i_val, NULL);
		s_val = "No comment";
		command_argument_add(ext_command, "comment", STRING, s_val, NULL);
		t_val = 0;
		command_argument_add(ext_command, "timestamp", TIMESTAMP, &t_val, NULL);
		command_register(ext_command, -1);

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT_WITH_TIMESTAMP;my_host;1;441;this is my comment, there are many like it but this one is mine;1234987650", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_OK == error, "No error when parsing proper commands");
		ok(1234987650 == *(time_t *)command_argument_get_value(ext_command, "timestamp"), "Timestamp value parsed successfully");
		command_destroy(ext_command);


		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT_WITH_TIMESTAMP;my_host;4;441;this is my comment, there are many like it but this one is mine;1234987650", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_VALIDATION_FAILURE == error, "Invalid BOOL value (4) is complained about");
		ok(NULL == ext_command, "No command returned for command with invalid argument values");

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT_WITH_TIMESTAMP;my_host;1;441;this is my comment, there are many like it but this one is mine;14:49", COMMAND_SYNTAX_NOKV, &error);

		ok(CMD_ERROR_PARSE_TYPE_MISMATCH == error, "Malformed timestamp value is complained about");
		ok(NULL == ext_command, "No command returned for command with argument type mismatch");

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT_WITH_TIMESTAMP;my_host;1;441;;", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_OK == error, "Missing arguments which have default values are not complained about");
		ok(!strcmp("No comment", command_argument_get_value(ext_command, "comment")), "Default value is used for missing argument");
		ok(t_val == *(time_t *)command_argument_get_value(ext_command, "timestamp"), "Default value is used for missing argument at end of arg string");
		command_destroy(ext_command);

		ext_command = command_parse("[1234567890] ADD_HOST_COMMENT_WITH_TIMESTAMP;some_host;;441;;13485799", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_PARSE_MISSING_ARG == error, "Missing arguments which don't have default values are complained about");
		ok(NULL == ext_command, "No command returned for command with missing argument and no default");

		ext_command = command_create("ADD_SVC_COMMENT", test__add_service_comment_handler, "This is a description for a command named CMD_ADD_SVC_COMMENT", NULL);
		command_argument_add(ext_command, "service", SERVICE, NULL, NULL);
		command_argument_add(ext_command, "persistent", BOOL, &b_val, NULL);
		command_argument_add(ext_command, "author", INTEGER, &i_val, NULL);
		command_argument_add(ext_command, "comment", STRING, s_val, NULL);
		command_register(ext_command, -1);

		ext_command = command_parse("[1234567890] ADD_SVC_COMMENT;my_host;NO_SUCH_SERVICE;1;441;this is my service comment, there are many like it but this one is mine", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_VALIDATION_FAILURE == error, "Invalid service is complained about");
		ok(NULL == ext_command, "No command returned for command with invalid service");

		ext_command = command_create("ADD_SVC_COMMENT_2", test__add_service_comment_handler, "This is a description for a command with a custom service validator", NULL);
		command_argument_add(ext_command, "service", SERVICE, NULL, custom_service_validator);
		command_argument_add(ext_command, "persistent", BOOL, &b_val, NULL);
		command_argument_add(ext_command, "author", INTEGER, &i_val, NULL);
		command_argument_add(ext_command, "comment", STRING, s_val, NULL);
		command_register(ext_command, -1);

		ext_command = command_parse("[1234567890] ADD_SVC_COMMENT_2;my_host;LETS_PRETEND_THIS_EXISTS;1;441;this is my service comment, there are many like it but this one is mine", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_OK == error, "Custom validator does not decline our invalid service");
		command_destroy(ext_command);


		ext_command = command_create("DEL_HOST_COMMENT", test__del_host_comment_handler, "This command is used to delete a specific host comment.", NULL);
		command_argument_add(ext_command, "comment_id", ULONG, NULL, NULL);
		command_register(ext_command, -1);

		ext_command = command_parse("[1234567890] DEL_HOST_COMMENT;10;Excess argument;snurre", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_PARSE_EXCESS_ARG == error, "Excess arguments are complained about");
		ok(ext_command == NULL, "No command returned for commands with excess arguments");

		ext_command = command_parse("[1234567890] DEL_HOST_COMMENT;10", COMMAND_SYNTAX_NOKV, &error);
		ok((unsigned long) 10 ==  *(unsigned long *)command_argument_get_value(ext_command, "comment_id"), "ULONG argument parsed correctly");
		command_destroy(ext_command);

		ext_command = command_create("DEL_HOST_COMMENT_2", test__del_host_comment_handler, "This command is used to delete a specific host comment.", "int=comment_id;str=string_arg");
		command_register(ext_command, -1);
		ext_command = command_parse("[1234567890] DEL_HOST_COMMENT_2;10;foobar", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_OK == error, "No error when parsing command created with argspec");
		ok(!strcmp("foobar", command_argument_get_value(ext_command, "string_arg")), "Can parse command created with argspec (string arg)");
		ok(10 == *(int *)command_argument_get_value(ext_command, "comment_id"), "Can parse command created with argspec (int arg)");
		command_destroy(ext_command);

		ext_command = command_parse("[1234567890] DEL_HOST_COMMENT_2;1;", COMMAND_SYNTAX_NOKV, &error);
		ok (ext_command == NULL, "Missing argument at end of arg string is complained about");
		ok(CMD_ERROR_PARSE_MISSING_ARG == error, "Missing argument at end of arg string raises the correct error");


		ext_command = command_create("DISABLE_NOTIFICATIONS", test__disable_notifications_handler,
			"Disables host and service notifications on a program-wide basis.", NULL);
		command_register(ext_command, -1);
		ext_command = command_parse("[1234567890] DISABLE_NOTIFICATIONS", COMMAND_SYNTAX_NOKV, &error);
		ok(ext_command != NULL, "No problem parsing commands with no arguments (when none required)");
		command_destroy(ext_command);

		ext_command = command_create("DO_THING_WITH_TIMEPERIOD", test__do_thing_with_timeperiod_handler,
				"Does a thing with a timeperiod", NULL);
		command_argument_add(ext_command, "timeperiod", TIMEPERIOD, NULL, NULL);
		command_register(ext_command, -1);

		ext_command = command_parse("[1234567890] DO_THING_WITH_TIMEPERIOD;24x8", COMMAND_SYNTAX_NOKV, &error);
		ok(ext_command == NULL, "No command returned when timeperiod arg is invalid");
		ok(CMD_ERROR_VALIDATION_FAILURE == error, "Validation error raised for invalid timeperiod");

		registered_timeperiod = find_timeperiod("24x7");
		assert(NULL != registered_timeperiod);
		ext_command = command_parse("[1234567890] DO_THING_WITH_TIMEPERIOD;24x7", COMMAND_SYNTAX_NOKV, &error);
		ok(ext_command != NULL, "Command returned when timeperiod arg is not invalid");
		ok(CMD_ERROR_OK == error, "Validation error not raised for valid timeperiod");
		ok(registered_timeperiod == command_argument_get_value(ext_command, "timeperiod"), "The correct timeperiod is returned");
		command_destroy(ext_command);

		/** CONTACT SETUP*/
		ext_command = command_create("FIND_CONTACT", test__do_thing_with_contact_handler, "Does a thing with contact", NULL);
		command_argument_add(ext_command, "contact", CONTACT, NULL, NULL);
		command_register(ext_command, -1);
		created_contact = find_contact("nagiosadmin");
		assert(NULL != created_contact);

		/** CONTACT TEST*/
		ext_command = command_parse("[1234567890] FIND_CONTACT;bango", COMMAND_SYNTAX_NOKV, &error);
		ok(ext_command == NULL, "No command returned when contact arg is invalid");
		ok(CMD_ERROR_VALIDATION_FAILURE == error, "Validation error raised for invalid contact");

		/** CONTACT TEST*/
		ext_command = command_parse("[1234567890] FIND_CONTACT;nagiosadmin", COMMAND_SYNTAX_NOKV, &error);
		ok(ext_command != NULL, "Command returned when contact arg is not invalid");
		ok(CMD_ERROR_OK == error, "Validation error not raised for valid contact");
		fetched_contact = command_argument_get_value(ext_command, "contact");
		ok(created_contact->name == fetched_contact->name, "The correct contact is returned");

		/** CONTACT TEARDOWN*/
		command_destroy(ext_command);

		ext_command = command_parse("[1234567890] _MY_CUSTOMARILY_CUSTOM_CUSTARD_COMMAND;foo;bar;baz;33", COMMAND_SYNTAX_NOKV, &error);
		ok(CMD_ERROR_CUSTOM_COMMAND == error, "Custom command reported as such");
		ok(ext_command != NULL, "Raw command returned when parsing custom command");
		ok(!strcmp("foo;bar;baz;33", command_raw_arguments(ext_command)), "Raw arguments properly set for custom command");
		ok(command_entry_time(ext_command) == (time_t)1234567890, "Entry time set for custom command");
		command_destroy(ext_command);

	}
	registered_commands_deinit();

}

void test_global_commands(void) {
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] SAVE_STATE_INFORMATION"), "core command: SAVE_STATE_INFORMATION");
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] READ_STATE_INFORMATION"), "core command: READ_STATE_INFORMATION");
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_NOTIFICATIONS"), "core command: DISABLE_NOTIFICATIONS");

	ok(!enable_notifications, "DISABLE_NOTIFICATIONS disables notifications");
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_NOTIFICATIONS"), "core command: ENABLE_NOTIFICATIONS");
	ok(enable_notifications, "ENABLE_NOTIFICATIONS enables notifications");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] STOP_EXECUTING_SVC_CHECKS"), "core command: STOP_EXECUTING_SVC_CHECKS");
	ok(!execute_service_checks, "STOP_EXECUTING_SVC_CHECKS disables service check execution");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] START_EXECUTING_SVC_CHECKS"), "core command: START_EXECUTING_SVC_CHECKS");
	ok(execute_service_checks, "START_EXECUTING_SVC_CHECKS enables service check execution");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] STOP_ACCEPTING_PASSIVE_SVC_CHECKS"), "core command: STOP_ACCEPTING_PASSIVE_SVC_CHECKS");
	ok(!accept_passive_service_checks, "STOP_ACCEPTING_PASSIVE_SVC_CHECKS disables passive service checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] START_ACCEPTING_PASSIVE_SVC_CHECKS"), "core command: START_ACCEPTING_PASSIVE_SVC_CHECKS");
	ok(accept_passive_service_checks, "START_ACCEPTING_PASSIVE_SVC_CHECKS enables passive service checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] STOP_OBSESSING_OVER_SVC_CHECKS"), "core command: STOP_OBSESSING_OVER_SVC_CHECKS");
	ok(!obsess_over_services, "STOP_OBSESSING_OVER_SVC_CHECKS disables service check obsession");


	ok(CMD_ERROR_OK == process_external_command1("[1234567890] START_OBSESSING_OVER_SVC_CHECKS"), "core command: START_OBSESSING_OVER_SVC_CHECKS");
	ok(obsess_over_services, "START_OBSESSING_OVER_SVC_CHECKS enables service check obsession");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] STOP_EXECUTING_HOST_CHECKS"), "core command: STOP_EXECUTING_HOST_CHECKS");
	ok(!execute_host_checks, "STOP_EXECUTING_HOST_CHECKS disables host check execution");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] START_EXECUTING_HOST_CHECKS"), "core command: START_EXECUTING_HOST_CHECKS");
	ok(execute_host_checks, "START_EXECUTING_HOST_CHECKS enables host check execution");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] STOP_ACCEPTING_PASSIVE_HOST_CHECKS"), "core command: STOP_ACCEPTING_PASSIVE_HOST_CHECKS");
	ok(!accept_passive_host_checks, "STOP_ACCEPTING_PASSIVE_HOST_CHECKS disables passive host checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] START_ACCEPTING_PASSIVE_HOST_CHECKS"), "core command: START_ACCEPTING_PASSIVE_HOST_CHECKS");
	ok(accept_passive_host_checks, "START_ACCEPTING_PASSIVE_HOST_CHECKS enables passive host checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] STOP_OBSESSING_OVER_HOST_CHECKS"), "core command: STOP_OBSESSING_OVER_HOST_CHECKS");
	ok(!obsess_over_hosts, "STOP_OBSESSING_OVER_HOST_CHECKS disables host check obsession");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] START_OBSESSING_OVER_HOST_CHECKS"), "core command: START_OBSESSING_OVER_HOST_CHECKS");
	ok(obsess_over_hosts, "START_OBSESSING_OVER_HOST_CHECKS enables host check obsession");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_EVENT_HANDLERS"), "core command: DISABLE_EVENT_HANDLERS");
	ok(!enable_event_handlers, "DISABLE_EVENT_HANDLERS disables event handlers");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_EVENT_HANDLERS"), "core command: ENABLE_EVENT_HANDLERS");
	ok(enable_event_handlers, "ENABLE_EVENT_HANDLERS enables event handlers");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_FLAP_DETECTION"), "core command: DISABLE_FLAP_DETECTION");
	ok(!enable_flap_detection, "DISABLE_FLAP_DETECTION disables flap detection");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_FLAP_DETECTION"), "core command: ENABLE_FLAP_DETECTION");
	ok(enable_flap_detection, "ENABLE_FLAP_DETECTION enables flap detection");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_SERVICE_FRESHNESS_CHECKS"), "core command: DISABLE_SERVICE_FRESHNESS_CHECKS");
	ok(!check_service_freshness, "DISABLE_SERVICE_FRESHNESS_CHECKS disables service freshness checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_SERVICE_FRESHNESS_CHECKS"), "core command: ENABLE_SERVICE_FRESHNESS_CHECKS");
	ok(check_service_freshness, "ENABLE_SERVICE_FRESHNESS_CHECKS enables service freshness checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_HOST_FRESHNESS_CHECKS"), "core command: ENABLE_HOST_FRESHNESS_CHECKS");
	ok(check_host_freshness, "ENABLE_HOST_FRESHNESS_CHECKS enables host freshness checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_HOST_FRESHNESS_CHECKS"), "core command: DISABLE_HOST_FRESHNESS_CHECKS");
	ok(!check_host_freshness, "DISABLE_HOST_FRESHNESS_CHECKS disables host freshness checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_PERFORMANCE_DATA"), "core command: ENABLE_PERFORMANCE_DATA");
	ok(process_performance_data, "ENABLE_PERFORMANCE_DATA enables performance data");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_PERFORMANCE_DATA"), "core command: DISABLE_PERFORMANCE_DATA");
	ok(!process_performance_data, "DISABLE_PERFORMANCE_DATA disables performance data");


}

void test_host_commands(void) {
	char *host_name = "host1";
	host *target_host = NULL;
	int pre = 0, prev_comment_id = next_comment_id;
	unsigned int prev_downtime_id;
	time_t check_time =0;
	char *cmdstr = NULL;
	target_host = find_host(host_name);
	target_host->obsess = FALSE;
	pre = number_of_host_comments(host_name);
	ok(CMD_ERROR_OK == process_external_command2(CMD_ADD_HOST_COMMENT, check_time, "host1;0;myself;my comment"), "process_external_command2: ADD_HOST_COMMENT");
	ok(pre+1 == number_of_host_comments(host_name), "ADD_HOST_COMMENT (through process_external_command2) adds a host comment");
	++pre;
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ADD_HOST_COMMENT;host1;0;myself;my comment"), "core command: ADD_HOST_COMMENT");
	ok(pre+1 == number_of_host_comments(host_name), "ADD_HOST_COMMENT adds a host comment");
	asprintf(&cmdstr, "[1234567890] DEL_HOST_COMMENT;%i", prev_comment_id);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: DEL_HOST_COMMENT");
	free(cmdstr);
	ok(pre == number_of_host_comments(host_name), "DEL_HOST_COMMENT deletes a host comment");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DELAY_HOST_NOTIFICATION;host1;9980283485"), "core command: DELAY_HOST_NOTIFICATION");
	ok(9980283485 == target_host->next_notification, "DELAY_HOST_NOTIFICATION delays host notifications");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_HOST_SVC_CHECKS;host1"), "core command: DISABLE_HOST_SVC_CHECKS");
	ok(!target_host->services->service_ptr->checks_enabled, "DISABLE_HOST_SVC_CHECKS disables active checks for services on a host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_HOST_SVC_CHECKS;host1"), "core command: ENABLE_HOST_SVC_CHECKS");
	ok(target_host->services->service_ptr->checks_enabled, "ENABLE_HOST_SVC_CHECKS enables active checks for services on a host");

	check_time = target_host->services->service_ptr->next_check - 20;
	asprintf(&cmdstr, "[1234567890] SCHEDULE_HOST_SVC_CHECKS;host1;%llu", (long long unsigned int)check_time);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: SCHEDULE_HOST_SVC_CHECKS");
	ok(check_time == target_host->services->service_ptr->next_check, "SCHEDULE_HOST_SVC_CHECKS schedules host service checks");
	free(cmdstr);

	assert(CMD_ERROR_OK == process_external_command1("[1234567890] ADD_HOST_COMMENT;host1;0;myself;comment 1"));
	assert(CMD_ERROR_OK == process_external_command1("[1234567890] ADD_HOST_COMMENT;host1;1;myself;comment 2"));
	assert(CMD_ERROR_OK == process_external_command1("[1234567890] ADD_HOST_COMMENT;host1;0;myself;comment 3"));
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DEL_ALL_HOST_COMMENTS;host1"), "core command: DEL_ALL_HOST_COMMENTS");
	ok(0 == number_of_host_comments(host_name), "DEL_ALL_HOST_COMMENTS deletes all host comments");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_HOST_NOTIFICATIONS;host1"), "core command: DISABLE_HOST_NOTIFICATIONS");
	ok(!target_host->notifications_enabled, "DISABLE_HOST_NOTIFICATIONS disables host notifications");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_HOST_NOTIFICATIONS;host1"), "core command: ENABLE_HOST_NOTIFICATIONS");
	ok(target_host->notifications_enabled, "ENABLE_HOST_NOTIFICATIONS enables host notifications");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST;host1"), "core command: DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST");
	ok(!((find_host("childofhost1"))->notifications_enabled), "DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST disables notifications beyond host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST;host1"), "core command: ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST");
	ok(((find_host("childofhost1"))->notifications_enabled), "ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST enables notifications beyond host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_HOST_SVC_NOTIFICATIONS;host1"), "core command: DISABLE_HOST_SVC_NOTIFICATIONS");
	ok(!(target_host->services->service_ptr->notifications_enabled), "DISABLE_HOST_SVC_NOTIFICATIONS disables notifications for services on a host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_HOST_SVC_NOTIFICATIONS;host1"), "core command: ENABLE_HOST_SVC_NOTIFICATIONS");
	ok(target_host->services->service_ptr->notifications_enabled, "ENABLE_HOST_SVC_NOTIFICATIONS enables notifications for services on a host");

	target_host->current_state = HOST_DOWN;
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ACKNOWLEDGE_HOST_PROBLEM;host1;2;0;0;myself;my ack comment"), "core command: ACKNOWLEDGE_HOST_PROBLEM");
	ok(target_host->problem_has_been_acknowledged, "ACKNOWLEDGE_HOST_PROBLEM acknowledges a host problem");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] REMOVE_HOST_ACKNOWLEDGEMENT;host1"), "core command: REMOVE_HOST_ACKNOWLEDGEMENT");
	ok(!target_host->problem_has_been_acknowledged, "REMOVE_HOST_ACKNOWLEDGEMENT removes a host acknowledgement");
	target_host->current_state = HOST_UP;

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_HOST_EVENT_HANDLER;host1"), "core command: DISABLE_HOST_EVENT_HANDLER");
	ok(!target_host->event_handler_enabled, "DISABLE_HOST_EVENT_HANDLER disables event handler for a host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_HOST_EVENT_HANDLER;host1"), "core command: ENABLE_HOST_EVENT_HANDLER");
	ok(target_host->event_handler_enabled, "ENABLE_HOST_EVENT_HANDLER enables event handler for a host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_HOST_CHECK;host1"), "core command: DISABLE_HOST_CHECK");
	ok(!target_host->checks_enabled, "DISABLE_HOST_CHECK disables active host checks");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_HOST_CHECK;host1"), "core command: ENABLE_HOST_CHECK");
	ok(target_host->checks_enabled, "ENABLE_HOST_CHECK enables active host checks");

	check_time = target_host->services->service_ptr->next_check + 2000;
	asprintf(&cmdstr, "[1234567890] SCHEDULE_FORCED_HOST_SVC_CHECKS;host1;%llu", (unsigned long long int)check_time);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: SCHEDULE_FORCED_HOST_SVC_CHECKS");
	ok(check_time == target_host->services->service_ptr->next_check, "SCHEDULE_FORCED_HOST_SVC_CHECKS schedules forced checks for services on a host");
	free(cmdstr);

	prev_downtime_id = next_downtime_id;
	asprintf(&cmdstr, "[1234567890] SCHEDULE_HOST_DOWNTIME;host1;%llu;%llu;1;0;0;myself;my downtime comment", (unsigned long long int)time(NULL), (unsigned long long int)time(NULL) + 1500);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: SCHEDULE_HOST_DOWNTIME");
	ok(prev_downtime_id != next_downtime_id, "SCHEDULE_HOST_DOWNTIME schedules one new downtime");
	ok(NULL != find_host_downtime(prev_downtime_id), "SCHEDULE_HOST_DOWNTIME schedules downtime for a host");
	free(cmdstr);

	asprintf(&cmdstr, "[1234567890] DEL_HOST_DOWNTIME;%i", prev_downtime_id);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: DEL_HOST_DOWNTIME");
	ok(!find_host_downtime(prev_downtime_id), "DEL_HOST_DOWNTIME deletes a scheduled host downtime");
	free(cmdstr);

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] DISABLE_HOST_FLAP_DETECTION;host1"), "core command: DISABLE_HOST_FLAP_DETECTION");
	ok(!target_host->flap_detection_enabled, "DISABLE_HOST_FLAP_DETECTION disables host flap detection");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] ENABLE_HOST_FLAP_DETECTION;host1"), "core command: ENABLE_HOST_FLAP_DETECTION");
	ok(target_host->flap_detection_enabled, "ENABLE_HOST_FLAP_DETECTION enables host flap detection");

	assert(NULL == find_service_downtime(0));
	asprintf(&cmdstr, "[1234567890] SCHEDULE_HOST_SVC_DOWNTIME;host1;%llu;%llu;1;0;0;myself;my downtime comment", (unsigned long long int)time(NULL), (unsigned long long int)time(NULL) + 1500);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: SCHEDULE_HOST_SVC_DOWNTIME");
	strcmp(host_name, find_service_downtime(0)->host_name);
	ok(0 == 0, "SCHEDULE_HOST_SVC_DOWNTIME schedules downtime for services on a host");
	free(cmdstr);

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] PROCESS_HOST_CHECK_RESULT;host1;1;some plugin output"), "core command: PROCESS_HOST_CHECK_RESULT");
	ok(target_host->current_state == HOST_DOWN, "PROCESS_HOST_CHECK_RESULT processes host check results");

	check_time = target_host->next_check - 20;
	asprintf(&cmdstr, "[1234567890] SCHEDULE_HOST_CHECK;host1;%llu", (unsigned long long int)check_time);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: SCHEDULE_HOST_CHECK");
	ok(check_time == target_host->next_check, "SCHEDULE_HOST_CHECK schedules a host check");
	free(cmdstr);

	asprintf(&cmdstr, "[1234567890] SCHEDULE_HOST_CHECK;host1;%llu", (unsigned long long int)check_time);
	ok(CMD_ERROR_OK == process_external_command1(cmdstr), "core command: SCHEDULE_FORCED_HOST_CHECK");
	ok(check_time == target_host->next_check, "SCHEDULE_FORCED_HOST_CHECK schedules a host check");
	free(cmdstr);

	assert(!(target_host->obsess));
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] START_OBSESSING_OVER_HOST;host1"), "core command: START_OBSESSING_OVER_HOST");
	ok(target_host->obsess, "START_OBSESSING_OVER_HOST enables OCHP for host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] STOP_OBSESSING_OVER_HOST;host1"), "core command: STOP_OBSESSING_OVER_HOST");
	ok(!target_host->obsess, "STOP_OBSESSING_OVER_HOST disables OCHP for host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] CHANGE_NORMAL_HOST_CHECK_INTERVAL;host1;42"), "core command: CHANGE_NORMAL_HOST_CHECK_INTERVAL");
	ok(42 == target_host->check_interval, "CHANGE_NORMAL_HOST_CHECK_INTERVAL changes the host check inteval for host");

	ok(CMD_ERROR_OK == process_external_command1("[1234567890] CHANGE_MAX_HOST_CHECK_ATTEMPTS;host1;9"), "core command: CHANGE_MAX_HOST_CHECK_ATTEMPTS");
	ok(9 == target_host->max_attempts, "CHANGE_MAX_HOST_CHECK_ATTEMPTS changes the maximum number of check attempts for host");
}

void test_core_commands(void) {
	/*setup configuration*/
	pre_flight_check(); /*without this, child_host links are not created and *_BEYOND_HOST test cases fail...*/
	registered_commands_init(200);
	register_core_commands();
	/* basic error propagation tests*/
	ok(CMD_ERROR_UNKNOWN_COMMAND == process_external_command1("[1234567890] NOT_A_REGISTERED_COMMAND"), "Unregistered core command is reported as such");
	ok(CMD_ERROR_MALFORMED_COMMAND == process_external_command1("[1234567890 A_MALFORMED_COMMAND"), "Malformed core command is reported as such");
	ok(CMD_ERROR_PARSE_MISSING_ARG == process_external_command1("[1234567890] ACKNOWLEDGE_HOST_PROBLEM"), "Missing arguments for core command is reported as such");
	ok(CMD_ERROR_PARSE_EXCESS_ARG == process_external_command1("[1234567890] ENABLE_HOST_SVC_NOTIFICATIONS;host1;some excess cruft"), "Excess arguments for core command is reported as such");
	ok(CMD_ERROR_PARSE_TYPE_MISMATCH == process_external_command1("[1234567890] ACKNOWLEDGE_HOST_PROBLEM;host1;author;comment;2;0;fillet knife"), "Argument type mismatch for core command is reported as such");
	ok(CMD_ERROR_VALIDATION_FAILURE == process_external_command1("[1234567890] DISABLE_HOST_EVENT_HANDLER;no-such-host"), "Invalid argument for core command is reported as such");
	ok(CMD_ERROR_OK == process_external_command1("[1234567890] _SOME_CUSTOM_COMMAND;some-custom-argument"), "Custom commands are not considered an error");

	test_global_commands();
	test_host_commands();
	registered_commands_deinit();
	free(config_file);
}

int main(int /*@unused@*/ argc, char /*@unused@*/ **arv)
{
	const char *test_config_file = get_default_config_file();
	plan_tests(490);
	init_event_queue();

	config_file_dir = nspath_absolute_dirname(test_config_file, NULL);
	assert(OK == read_main_config_file(test_config_file));
	assert(OK == read_all_object_data(test_config_file));
	assert(OK == initialize_downtime_data());
	assert(OK == initialize_retention_data(get_default_config_file()));
	assert(OK == read_initial_state_information());
	test_register();
	test_parsing();
	test_core_commands();
	return exit_status();
}
