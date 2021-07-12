#include <check.h>
#include <glib.h>
#include <stdio.h>
#include "naemon/objects_service.h"
#include "naemon/objects_command.h"
#include "naemon/objects_host.h"
#include "naemon/downtime.h"
#include "naemon/events.h"
#include "naemon/xrddefault.c"

#define TARGET_SERVICE_NAME "my_service"
#define TARGET_HOST_NAME "my_host"

static host *hst;
static command *cmd;
static service *svc;

/* This is separate due to it being required from inside the tests. */
void setup_objects(void)
{

	init_objects_command(1);
	cmd = create_command("my_command", "/bin/true");
	ck_assert(cmd != NULL);
	register_command(cmd);

	init_objects_host(1);
	hst = create_host(TARGET_HOST_NAME);
	ck_assert(hst != NULL);
	hst->check_command_ptr = cmd;
	hst->retain_status_information = TRUE;
	register_host(hst);

	init_objects_service(1);
	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	svc->retain_status_information = TRUE;
	register_service(svc);

}

void teardown_objects(void)
{

	destroy_objects_command();
	destroy_objects_host();
	destroy_objects_service(TRUE);

}

void setup(void)
{

	init_event_queue();
	setup_objects();

	retain_state_information = TRUE;
	retention_file = nm_strdup("/tmp/retention.dat");
	temp_file = nm_strdup("/tmp/retention.tmp");

	initialize_retention_data();

}

void teardown(void)
{

	teardown_objects();
	cleanup_retention_data();
	destroy_event_queue();

}

START_TEST(retention_data_for_hosts_long_output)
{

	const char *long_output = g_strescape("This is a long \n plugin output \n of some sort \n and such \n", "");

	hst->long_plugin_output = strdup(long_output);

	ck_assert(OK == save_state_information(0)); /* this parameter does nothing ... */

	teardown_objects();
	setup_objects();

	ck_assert(OK == read_initial_state_information());
	ck_assert_str_eq(hst->long_plugin_output, long_output);

}
END_TEST

START_TEST(retention_data_for_services_long_output)
{

	const char *long_output = g_strescape("This is a long \n plugin output \n of some sort \n and such \n", "");

	svc->long_plugin_output = strdup(long_output);

	ck_assert(OK == save_state_information(0));

	teardown_objects();
	setup_objects();

	ck_assert(OK == read_initial_state_information());
	ck_assert_str_eq(svc->long_plugin_output, long_output);

}
END_TEST

Suite *
retention_suite(void)
{
	Suite *s = suite_create("Retention data");

	TCase *tc_retention_data_for_hosts_long_output = tcase_create("Retention data for hosts");
	TCase *tc_retention_data_for_services_long_output = tcase_create("Retention data for services");

	tcase_add_checked_fixture(tc_retention_data_for_hosts_long_output, setup, teardown);
	tcase_add_checked_fixture(tc_retention_data_for_services_long_output, setup, teardown);

	tcase_add_test(tc_retention_data_for_hosts_long_output, retention_data_for_hosts_long_output);
	tcase_add_test(tc_retention_data_for_services_long_output, retention_data_for_services_long_output);

	suite_add_tcase(s, tc_retention_data_for_hosts_long_output);
	suite_add_tcase(s, tc_retention_data_for_services_long_output);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = retention_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
