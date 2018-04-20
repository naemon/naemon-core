#include <check.h>
#include <glib.h>
#include "naemon/checks.h"
#include "naemon/checks_host.h"
#include "naemon/checks_service.h"
#include "naemon/globals.h"
#include "naemon/logging.h"
#include "naemon/events.h"
#include <sys/time.h>
#include <fcntl.h>

#define TARGET_SERVICE_NAME "my_service"
#define TARGET_HOST_NAME "my_host"

static host *hst;
static service *svc;
static command *cmd;

void setup (void) {
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
	hst->current_attempt = 1;
	register_host(hst);

	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	svc->current_attempt = 1;
	register_service(svc);
}

void teardown (void) {

	destroy_objects_command();
	destroy_objects_service();
	destroy_objects_host();
	destroy_event_queue();
}

START_TEST(host_service_down_then_host_up_service_remain_down)
{
	/* In this test case, the check results come in following order:
	 *    1. service check failure
	 *    2. host check failure
	 *    3. service check failure
	 *    4. host check failure
	 *    5. service check failure and service is hard down state
	 *    6. host check failure and host is hard down state
	 *    7. host check success and host is up now
	 *    8. service check failure and service still in hard down state
	 *    9. service check ok and service is up again
	 */
	struct check_result svc_cr, hst_cr;

	hst_cr.object_check_type = HOST_CHECK;
	hst_cr.host_name = TARGET_HOST_NAME;
	hst_cr.service_description = NULL;
	hst_cr.check_type = CHECK_TYPE_ACTIVE;
	hst_cr.check_options = 0;
	hst_cr.scheduled_check = TRUE;
	hst_cr.latency = 10;
	hst_cr.early_timeout = FALSE;
	hst_cr.exited_ok = TRUE;
	hst_cr.return_code = STATE_CRITICAL;
	hst_cr.output = "is DOWN - rta: nan, lost 100%|pkt=5;5;5;5;5 rta=0.0;2000.000;2000.000;; pl=100%;95;100;;";
	hst_cr.source = NULL;
	hst_cr.engine = NULL;

	hst->current_state = STATE_UP;
	svc->current_state = STATE_UP;

	hst->max_attempts = 3;
	svc->max_attempts = 3;

	svc_cr.object_check_type = SERVICE_CHECK;
	svc_cr.host_name = TARGET_HOST_NAME;
	svc_cr.service_description = TARGET_SERVICE_NAME;
	svc_cr.check_type = CHECK_TYPE_ACTIVE;
	svc_cr.check_options = 0;
	svc_cr.scheduled_check = TRUE;
	svc_cr.latency = 10;
	svc_cr.early_timeout = FALSE;
	svc_cr.exited_ok = TRUE;
	svc_cr.return_code = STATE_CRITICAL;
	svc_cr.output = "CHECK_NRPE: Socket timeout after 10 seconds.";
	svc_cr.source = NULL;
	svc_cr.engine = NULL;

	ck_assert_int_eq(0,gettimeofday(&(hst_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.finish_time) , NULL));

	ck_assert_int_eq(0,gettimeofday(&(svc_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.finish_time) , NULL));

	/* First feed a failure check result for service */
	process_check_result(&svc_cr);
	ck_assert(svc->state_type == SOFT_STATE);
	ck_assert(svc->current_state == STATE_CRITICAL);
	ck_assert(svc->current_attempt == 1);

	process_check_result(&hst_cr);
	ck_assert(hst->state_type == SOFT_STATE);
	ck_assert(hst->current_state == STATE_DOWN);
	ck_assert(hst->current_attempt == 1);

	/* update the timestamp for the check results */
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.finish_time) , NULL));

	ck_assert_int_eq(0,gettimeofday(&(svc_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.finish_time) , NULL));
	process_check_result(&svc_cr);
	ck_assert(svc->state_type == SOFT_STATE);
	ck_assert(svc->current_state == STATE_CRITICAL);
	ck_assert(svc->current_attempt == 2);

	process_check_result(&hst_cr);
	ck_assert(hst->state_type == SOFT_STATE);
	ck_assert(hst->current_state == STATE_DOWN);
	ck_assert(hst->current_attempt == 2);

	/* update the timestamp for the check results */
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.finish_time) , NULL));

	ck_assert_int_eq(0,gettimeofday(&(svc_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.finish_time) , NULL));
	process_check_result(&svc_cr);
	ck_assert(svc->state_type == HARD_STATE);
	ck_assert(svc->current_state == STATE_CRITICAL);
	ck_assert(svc->current_attempt == 3);

	process_check_result(&hst_cr);
	ck_assert(hst->state_type == HARD_STATE);
	ck_assert(hst->current_state == STATE_DOWN);
	ck_assert(hst->current_attempt == 3);

	/* Sending host up CR now */
	hst_cr.return_code = STATE_UP;
	hst_cr.output = "OK - xx.xxx.xxx.xxx responds to ICMP. Packet 1, rtt 0.325ms";
	/* update the timestamp for the check results */
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.finish_time) , NULL));

	ck_assert_int_eq(0,gettimeofday(&(svc_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.finish_time) , NULL));

	process_check_result(&hst_cr);
	ck_assert(hst->state_type == HARD_STATE);
	ck_assert(hst->current_state == STATE_OK);
	ck_assert(hst->current_attempt == 1);

	process_check_result(&svc_cr);
	ck_assert(svc->state_type == HARD_STATE);
	ck_assert(svc->current_state == STATE_CRITICAL);
	ck_assert(svc->current_attempt == 3);

	svc_cr.return_code = STATE_OK;
	svc_cr.output = "CPU OK : idle 100.00%";
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.finish_time) , NULL));
	process_check_result(&svc_cr);
	ck_assert(svc->state_type == HARD_STATE);
	ck_assert(svc->current_state == STATE_OK);
	ck_assert(svc->current_attempt == 1);
}
END_TEST

START_TEST(host_service_down_then_host_service_up)
{
	/* In this test case, the check results come in following order:
	 *    1. host check failure
	 *    2. service check failure
	 *    3. host check failure
	 *    4. host check success and host is up now
	 *    5. service check ok and service is in up state now
	 */
	struct check_result svc_cr, hst_cr;

	hst_cr.object_check_type = HOST_CHECK;
	hst_cr.host_name = TARGET_HOST_NAME;
	hst_cr.service_description = NULL;
	hst_cr.check_type = CHECK_TYPE_ACTIVE;
	hst_cr.check_options = 0;
	hst_cr.scheduled_check = TRUE;
	hst_cr.latency = 10;
	hst_cr.early_timeout = FALSE;
	hst_cr.exited_ok = TRUE;
	hst_cr.return_code = STATE_CRITICAL;
	hst_cr.output = "is DOWN - rta: nan, lost 100%|pkt=5;5;5;5;5 rta=0.0;2000.000;2000.000;; pl=100%;95;100;;";
	hst_cr.source = NULL;
	hst_cr.engine = NULL;

	hst->current_state = STATE_UP;
	svc->current_state = STATE_UP;

	hst->max_attempts = 3;
	svc->max_attempts = 3;

	svc_cr.object_check_type = SERVICE_CHECK;
	svc_cr.host_name = TARGET_HOST_NAME;
	svc_cr.service_description = TARGET_SERVICE_NAME;
	svc_cr.check_type = CHECK_TYPE_ACTIVE;
	svc_cr.check_options = 0;
	svc_cr.scheduled_check = TRUE;
	svc_cr.latency = 10;
	svc_cr.early_timeout = FALSE;
	svc_cr.exited_ok = TRUE;
	svc_cr.return_code = STATE_CRITICAL;
	svc_cr.output = "CHECK_NRPE: Socket timeout after 10 seconds.";
	svc_cr.source = NULL;
	svc_cr.engine = NULL;

	ck_assert_int_eq(0,gettimeofday(&(hst_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.finish_time) , NULL));

	ck_assert_int_eq(0,gettimeofday(&(svc_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.finish_time) , NULL));

	process_check_result(&hst_cr);
	ck_assert(hst->state_type == SOFT_STATE);
	ck_assert(hst->current_state == STATE_DOWN);
	ck_assert(hst->current_attempt == 1);

	/* feed a failure check result for service */
	process_check_result(&svc_cr);
	ck_assert(svc->state_type == SOFT_STATE);
	ck_assert(svc->current_state == STATE_CRITICAL);
	ck_assert(svc->current_attempt == 1);

	/* update the timestamp for the check results */
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.finish_time) , NULL));

	process_check_result(&hst_cr);
	ck_assert(hst->state_type == SOFT_STATE);
	ck_assert(hst->current_state == STATE_DOWN);
	ck_assert(hst->current_attempt == 2);

	/* Sending host up CR now */
	hst_cr.return_code = STATE_UP;
	hst_cr.output = "OK - xx.xxx.xxx.xxx responds to ICMP. Packet 1, rtt 0.325ms";
	/* update the timestamp for the check results */
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(hst_cr.finish_time) , NULL));

	process_check_result(&hst_cr);
	ck_assert(hst->state_type == HARD_STATE);
	ck_assert(hst->current_state == STATE_OK);
	ck_assert(hst->current_attempt == 1);

	svc_cr.return_code = STATE_OK;
	svc_cr.output = "CPU OK : idle 100.00%";
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.start_time) , NULL));
	ck_assert_int_eq(0,gettimeofday(&(svc_cr.finish_time) , NULL));
	process_check_result(&svc_cr);
	ck_assert(svc->state_type == HARD_STATE);
	ck_assert(svc->current_state == STATE_OK);
	ck_assert(svc->current_attempt == 1);
}
END_TEST


Suite*
service_state_suite(void)
{
	Suite *s = suite_create("Serivce state consistency");

	TCase *tc_service_states_while_host_down = tcase_create("Service states while host down");

	tcase_add_checked_fixture(tc_service_states_while_host_down, setup, teardown);

	tcase_add_test(tc_service_states_while_host_down, host_service_down_then_host_up_service_remain_down);
	tcase_add_test(tc_service_states_while_host_down, host_service_down_then_host_service_up);

	suite_add_tcase(s, tc_service_states_while_host_down);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = service_state_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

