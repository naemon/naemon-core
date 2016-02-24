#include <check.h>
#include <glib.h>
#include "naemon/objects_service.h"
#include "naemon/checks.h"
#include "naemon/broker.h"
#include "naemon/neberrors.h"
#define TARGET_SERVICE_NAME "my_service"
#define TARGET_HOST_NAME "my_host"

static gboolean g_service_was_checked = FALSE;
static gboolean g_host_was_checked = FALSE;

int my_broker_host_check(int type, int flags, int attr, host *hst, int check_type, int state, int state_type, struct timeval start_time, struct timeval end_time, char *cmd, double latency, double exectime, int timeout, int early_timeout, int retcode, char *cmdline, char *output, char *long_output, char *perfdata, check_result *cr) {
	if (type == NEBTYPE_HOSTCHECK_INITIATE) {
		ck_assert_str_eq(TARGET_HOST_NAME, hst->name);
		g_host_was_checked = TRUE;
		return NEBERROR_CALLBACKOVERRIDE;
	}
	return 0;
}

int my_broker_service_check(int type, int flags, int attr, service *svc, int check_type, struct timeval start_time, struct timeval end_time, char *cmd, double latency, double exectime, int timeout, int early_timeout, int retcode, char *cmdline, check_result *cr) {
	if (type == NEBTYPE_SERVICECHECK_INITIATE) {
		ck_assert_str_eq(TARGET_SERVICE_NAME, svc->description);
		g_service_was_checked = TRUE;
		return NEBERROR_CALLBACKOVERRIDE;
	}
	return 0;
}

#define broker_host_check my_broker_host_check
#include "naemon/checks_host.c"
#undef broker_host_check

#define broker_service_check my_broker_service_check
#include "naemon/checks_service.c"
#undef broker_service_check

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
	register_host(hst);

	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	register_service(svc);

}

void teardown (void) {
	destroy_event_queue();
	destroy_objects_command();
	destroy_objects_service();
	destroy_objects_host();
}

START_TEST(service_freshness_checking)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = svc
	};

	svc->check_options = CHECK_OPTION_FORCE_EXECUTION;
	svc->checks_enabled = FALSE;
	svc->check_freshness = TRUE;
	svc->max_attempts = 1;
	svc->check_interval = 5.0;
	svc->freshness_threshold = 60;

	handle_service_check_event(&ep);
	ck_assert(g_service_was_checked);
}
END_TEST


START_TEST(host_freshness_checking)
{
	struct nm_event_execution_properties ep = {
		.execution_type = EVENT_EXEC_NORMAL,
		.event_type = EVENT_TYPE_TIMED,
		.user_data = hst
	};

	hst->check_options = CHECK_OPTION_FORCE_EXECUTION;
	hst->checks_enabled = FALSE;
	hst->check_freshness = TRUE;
	hst->max_attempts = 1;
	hst->check_interval = 5.0;
	hst->freshness_threshold = 60;

	handle_host_check_event(&ep);
	ck_assert(g_host_was_checked);
}
END_TEST

Suite*
service_checks_suite(void)
{
	Suite *s = suite_create("Check scheduling");
	TCase *tc_freshness_checking = tcase_create("Freshness checking");
	tcase_add_checked_fixture(tc_freshness_checking, setup, teardown);
	tcase_add_test(tc_freshness_checking, service_freshness_checking);
	tcase_add_test(tc_freshness_checking, host_freshness_checking);
	suite_add_tcase(s, tc_freshness_checking);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = service_checks_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
