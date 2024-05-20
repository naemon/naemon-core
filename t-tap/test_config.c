/*****************************************************************************
 *
 * test_config.c - Test configuration loading (and retention data)
 *
 * Program: Naemon Core Testing
 * License: GPL
 *
 * Description:
 *
 * Tests Naemon's configuration loading and retention data routine
 *
 *****************************************************************************/

#include "config.h"
#include <stdarg.h>
#include <string.h>
#include "naemon/utils.h"
#include "naemon/common.h"
#include "naemon/objects.h"
#include "naemon/comments.h"
#include "naemon/configuration.h"
#include "naemon/defaults.h"
#include "naemon/downtime.h"
#include "naemon/events.h"
#include "naemon/globals.h"
#include "naemon/statusdata.h"
#include "naemon/macros.h"
#include "naemon/sretention.h"
#include "naemon/perfdata.h"
#include "naemon/broker.h"
#include "naemon/nebmods.h"
#include "naemon/nebmodules.h"
#include "naemon/xrddefault.h"
#include "naemon/nm_alloc.h"
#include "tap.h"

int main(int argc, char **argv)
{
	int result;
	int c = 0;
	struct host *host1, *host2;
	hostgroup *temp_hostgroup = NULL;

	plan_tests(19);

	/* reset program variables */
	reset_variables();

	/*
	 * avoid updating the checked-in retention data
	 * file when testing
	 */
	retain_state_information = FALSE;

	printf("Reading configuration data...\n");

	config_file = strdup(TESTDIR "naemon.cfg");
	config_file_dir = nspath_absolute_dirname(config_file, NULL);
	/* read in the configuration files (main config file, resource and object config files) */
	result = read_main_config_file(config_file);
	ok(result == OK, "Read main configuration file okay - if fails, use nagios -v to check");

	result = read_all_object_data(config_file);
	ok(result == OK, "Read all object config files");

	result = pre_flight_check();
	ok(result == OK, "Preflight check okay");

	for (temp_hostgroup = hostgroup_list; temp_hostgroup != NULL; temp_hostgroup = temp_hostgroup->next) {
		c++;
		//printf("Hostgroup=%s\n", temp_hostgroup->group_name);
	}
	ok(c == 2, "Found all hostgroups");

	temp_hostgroup = find_hostgroup("hostgroup1");
	temp_hostgroup = find_hostgroup("hostgroup2");
	host1 = find_host("host1");
	host2 = find_host("host2");
	ok(host1 != NULL && host2 != NULL, "find_host() should work");
	ok(host1->current_state == 0, "State is assumed OK on initial load");
	ok(host1->notifications_enabled == 1, "host1 notifications_enabled set from config");
	ok(host2->notifications_enabled == 1, "host2 notifications_enabled set from config");

	init_event_queue();

	initialize_retention_data();
	initialize_downtime_data();
	initialize_comment_data();

	ok(xrddefault_read_state_information() == OK, "Reading retention data");

	ok(host1->current_state == 1, "State changed due to retention file settings");
	ok(host1->notifications_enabled == 1, "Config change should override notifications_enabled for host1");
	ok(host2->notifications_enabled == 0, "Retention data should win on no config change");

	ok(find_host_comment(418) != NULL, "Found host comment id 418");
	ok(find_service_comment(419) != NULL, "Found service comment id 419");
	ok(find_service_comment(420) == NULL, "Did not find service comment id 420 as not persistent");
	ok(find_host_comment(1234567888) == NULL, "No such host comment");

	ok(find_host_downtime(1102) != NULL, "Found host downtime id 1102");
	ok(find_service_downtime(1110) != NULL, "Found service downtime 1110");
	ok(find_host_downtime(1234567888) == NULL, "No such host downtime");

	cleanup_downtime_data();
	cleanup_retention_data();
	cleanup();

	nm_free(config_file);

	return exit_status();
}
