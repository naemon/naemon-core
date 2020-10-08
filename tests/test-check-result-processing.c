#include <check.h>
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
	destroy_objects_service();
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
	suite_add_tcase(s, tc_process);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
