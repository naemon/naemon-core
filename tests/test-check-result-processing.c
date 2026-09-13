#include <check.h>
#include <string.h>
#include "naemon/checks.h"
#include "naemon/checks_host.h"
#include "naemon/checks_service.h"
#include "naemon/globals.h"
#include "naemon/logging.h"
#include "naemon/events.h"

#define TARGET_SERVICE_NAME "my_service"
#define TARGET_HOST_NAME "my_host"

static host *hst;
static service *svc;
static command *cmd;
void setup(void)
{

	init_event_queue();
	init_objects_host(1);
	init_objects_service(1);
	init_objects_command(1);

	cmd = create_command("my_command", "/bin/true");
	ck_assert(cmd != NULL);
	register_command(cmd);

	hst = create_host(TARGET_HOST_NAME);
	ck_assert(hst != NULL);
	hst->check_command_ptr = cmd;
	hst->check_command = nm_strdup("something or other");
	register_host(hst);

	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	svc->accept_passive_checks = TRUE;
	register_service(svc);

}

void teardown(void)
{
	destroy_event_queue();
	destroy_objects_command();
	destroy_objects_service(TRUE);
	destroy_objects_host();
}

START_TEST(host_soft_to_hard)
{
	struct host cur = {
		.name = "a fake host",
		.state_type = SOFT_STATE,
		.current_state = STATE_DOWN,
		.max_attempts = 3,
		.current_attempt = 2,
		.has_been_checked = 0,
		.check_command = "dummy_command required",
	};
	struct check_result cr = {
		.output = "The output",
		.exited_ok = TRUE,
		.return_code = STATE_CRITICAL,
		.check_type = CHECK_TYPE_ACTIVE,
	};
	update_host_state_post_check(&cur, &cr);
	ck_assert(cur.current_state == STATE_DOWN);
	ck_assert(cur.has_been_checked == 1);
	ck_assert(cur.state_type == HARD_STATE);
	free(cur.plugin_output);
	free(cur.long_plugin_output);
	free(cur.perf_data);
}
END_TEST

START_TEST(spool_file_processing)
{
	int result;
	FILE *fp;
	char test_spool_file[] = "/tmp/naemon-spool-test-XXXXXX";
	int fd;
	time_t now = time(NULL);

	fd = mkstemp(test_spool_file);
	close(fd);
	fp = fopen(test_spool_file, "a");
	fprintf(fp,
	        "file_time=%ld\n"
	        "\n"
	        "host_name=%s\n"
	        "service_description=%s\n"
	        "check_type=1\n"
	        "check_options=0\n"
	        "scheduled_check=0\n"
	        "latency=0.000000\n"
	        "start_time=%ld.000000\n"
	        "finish_time=%ld.000000\n"
	        "early_timeout=0\n"
	        "exited_ok=1\n"
	        "return_code=0\n"
	        "output=testoutput\\nwith multiline\\nand \\backslash|perf=0.001s\n",
	        now,
	        TARGET_HOST_NAME,
	        TARGET_SERVICE_NAME,
	        now,
	        now
	       );
	fclose(fp);
	result = process_check_result_file(test_spool_file);
	ck_assert(result == OK);
	ck_assert_str_eq(svc->plugin_output, "testoutput");
	ck_assert_str_eq(svc->long_plugin_output, "with multiline\\nand \\backslash");
	ck_assert_str_eq(svc->perf_data, "perf=0.001s");
	unlink(test_spool_file);
}
END_TEST

/*
 * process_check_result() can find the object either from cr->object_ptr or,
 * when that is NULL, by looking host_name/service_description up. Both routes
 * have to end on the same object, and an unknown name still has to fail.
 */
START_TEST(result_routed_by_object_ptr)
{
	check_result cr;

	init_check_result(&cr);
	cr.object_check_type = SERVICE_CHECK;
	cr.check_type = CHECK_TYPE_PASSIVE;
	cr.object_ptr = svc;
	/* deliberately no host_name or service_description: the pointer is enough */
	cr.return_code = STATE_WARNING;
	cr.output = nm_strdup("routed by pointer");
	cr.exited_ok = TRUE;

	ck_assert(OK == process_check_result(&cr));
	ck_assert_str_eq(svc->plugin_output, "routed by pointer");
	free_check_result(&cr);
}
END_TEST

START_TEST(result_routed_by_name)
{
	check_result cr;

	init_check_result(&cr);
	cr.object_check_type = SERVICE_CHECK;
	cr.check_type = CHECK_TYPE_PASSIVE;
	/* object_ptr left NULL by init_check_result(), so the name is used */
	cr.host_name = nm_strdup(TARGET_HOST_NAME);
	cr.service_description = nm_strdup(TARGET_SERVICE_NAME);
	cr.return_code = STATE_CRITICAL;
	cr.output = nm_strdup("routed by name");
	cr.exited_ok = TRUE;

	ck_assert(OK == process_check_result(&cr));
	ck_assert_str_eq(svc->plugin_output, "routed by name");
	free_check_result(&cr);
}
END_TEST

START_TEST(result_with_unknown_name_still_fails)
{
	check_result cr;

	init_check_result(&cr);
	cr.object_check_type = SERVICE_CHECK;
	cr.check_type = CHECK_TYPE_PASSIVE;
	cr.host_name = nm_strdup("no such host");
	cr.service_description = nm_strdup("no such service");
	cr.return_code = STATE_CRITICAL;
	cr.output = nm_strdup("should not be stored anywhere");
	cr.exited_ok = TRUE;

	ck_assert(ERROR == process_check_result(&cr));
	free_check_result(&cr);
}
END_TEST

/*
 * init_check_result() used to assign fields one by one and missed output_file,
 * timeout and rusage, which stayed whatever was on the caller's stack. Pin
 * that it now clears the whole struct, since the object_ptr above is only safe
 * to dereference because of it.
 */
START_TEST(init_check_result_clears_everything)
{
	check_result cr;

	memset(&cr, 0xa5, sizeof(cr));
	init_check_result(&cr);

	ck_assert(cr.object_ptr == NULL);
	ck_assert(cr.output_file == NULL);
	ck_assert(cr.output_file_fp == NULL);
	ck_assert(cr.output == NULL);
	ck_assert(cr.host_name == NULL);
	ck_assert(cr.service_description == NULL);
	ck_assert(cr.engine == NULL);
	ck_assert(cr.source == NULL);
	ck_assert(cr.timeout == 0);
	ck_assert(cr.latency == 0.0);
	ck_assert(cr.rusage.ru_utime.tv_sec == 0);
	ck_assert(cr.rusage.ru_stime.tv_sec == 0);

	/* and the defaults that are deliberately not zero */
	ck_assert(cr.object_check_type == HOST_CHECK);
	ck_assert(cr.check_type == CHECK_TYPE_ACTIVE);
	ck_assert(cr.exited_ok == TRUE);
}
END_TEST

int main(int argc, char **argv)
{
	int number_failed = 0;
	Suite *s;
	SRunner *sr;
	TCase *tc_process = tcase_create("Result processing");
	tcase_add_checked_fixture(tc_process, setup, teardown);

	debug_level = -1;
	debug_verbosity = 5;
	debug_file = "/dev/stdout";
	open_debug_log();
	max_debug_file_size = 0;

	s = suite_create("Check results");
	tcase_add_test(tc_process, host_soft_to_hard);
	tcase_add_test(tc_process, spool_file_processing);
	tcase_add_test(tc_process, result_routed_by_object_ptr);
	tcase_add_test(tc_process, result_routed_by_name);
	tcase_add_test(tc_process, result_with_unknown_name_still_fails);
	tcase_add_test(tc_process, init_check_result_clears_everything);
	suite_add_tcase(s, tc_process);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
