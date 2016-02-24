#include <check.h>
#include "naemon/checks.h"
#include "naemon/checks_host.h"
#include "naemon/checks_service.h"
#include "naemon/globals.h"
#include "naemon/logging.h"

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

int main(int argc, char **argv)
{
	int number_failed = 0;
	Suite *s;
	SRunner *sr;
	TCase *tc_process = tcase_create("Result processing");

	debug_level = -1;
	debug_verbosity = 5;
	debug_file = "/dev/stdout";
	open_debug_log();
	max_debug_file_size = 0;

	s = suite_create("Check results");
	tcase_add_test(tc_process, host_soft_to_hard);
	suite_add_tcase(s, tc_process);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
