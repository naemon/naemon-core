#include <check.h>
#include <glib.h>
#include "naemon/checks.h"
#include "naemon/checks_host.c"
#include "naemon/checks_service.c"

#define TARGET_SERVICE_NAME "my_service"
#define TARGET_DEP_SERVICE_NAME "my_dependency"
#define TARGET_HOST_NAME "my_host"
#define TARGET_DEP_HOST_NAME "my_dep_host"

static host *hst;
static host *dep_hst;
static service *svc;
static service *dep_svc;
static command *cmd;
void setup(void)
{

	init_event_queue();
	init_objects_host(2);
	init_objects_service(2);
	init_objects_command(1);

	cmd = create_command("my_command", "/bin/true");
	ck_assert(cmd != NULL);
	register_command(cmd);

	hst = create_host(TARGET_HOST_NAME);
	ck_assert(hst != NULL);
	hst->check_command_ptr = cmd;
	hst->check_command = nm_strdup("something or other");
	register_host(hst);

	dep_hst = create_host(TARGET_DEP_HOST_NAME);
	ck_assert(dep_hst != NULL);
	dep_hst->check_command_ptr = cmd;
	dep_hst->check_command = nm_strdup("something or other");
	register_host(dep_hst);

	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	register_service(svc);

	dep_svc = create_service(hst, TARGET_DEP_SERVICE_NAME);
	ck_assert(dep_svc != NULL);
	dep_svc->check_command_ptr = cmd;
	register_service(dep_svc);
}

void teardown(void)
{
	destroy_event_queue();
	destroy_objects_command();
	destroy_objects_service();
	destroy_objects_host();
}

START_TEST(host_execution_no_dependency)
{
	int result;

	result = check_host_dependencies(hst, EXECUTION_DEPENDENCY);

	ck_assert(result == DEPENDENCIES_OK);
}
END_TEST


START_TEST(service_execution_no_dependency)
{
	int result;

	result = check_service_dependencies(svc, EXECUTION_DEPENDENCY);

	ck_assert(result == DEPENDENCIES_OK);
}
END_TEST

START_TEST(host_execution_dependency_pending)
{
	int result;
	add_host_dependency(TARGET_HOST_NAME, TARGET_DEP_HOST_NAME, EXECUTION_DEPENDENCY, 0, OPT_PENDING, NULL);

	dep_hst->has_been_checked = FALSE;
	result = check_host_dependencies(hst, EXECUTION_DEPENDENCY);
	ck_assert(result == DEPENDENCIES_FAILED);
}
END_TEST

START_TEST(host_execution_dependency_down)
{
	int result;
	add_host_dependency(TARGET_HOST_NAME, TARGET_DEP_HOST_NAME, EXECUTION_DEPENDENCY, 0, OPT_DOWN, NULL);
	dep_hst->state_type = HARD_STATE;
	dep_hst->current_state = STATE_UP;
	dep_hst->has_been_checked = TRUE;
	result = check_host_dependencies(hst, EXECUTION_DEPENDENCY);
	ck_assert(result == DEPENDENCIES_OK);

	dep_hst->current_state = STATE_DOWN;
	result = check_host_dependencies(hst, EXECUTION_DEPENDENCY);
	ck_assert(result == DEPENDENCIES_FAILED);
}
END_TEST

START_TEST(service_execution_dependency_pending)
{
	int result;
	add_service_dependency(TARGET_HOST_NAME, TARGET_SERVICE_NAME, TARGET_HOST_NAME, TARGET_DEP_SERVICE_NAME, EXECUTION_DEPENDENCY, 0, OPT_PENDING, NULL);

	dep_svc->has_been_checked = FALSE;
	result = check_service_dependencies(svc, EXECUTION_DEPENDENCY);
	ck_assert(result == DEPENDENCIES_FAILED);
}
END_TEST

START_TEST(service_execution_dependency_critical)
{
	int result;
	add_service_dependency(TARGET_HOST_NAME, TARGET_SERVICE_NAME, TARGET_HOST_NAME, TARGET_DEP_SERVICE_NAME, EXECUTION_DEPENDENCY, 0, OPT_CRITICAL, NULL);
	dep_svc->state_type = HARD_STATE;
	dep_svc->current_state = STATE_OK;
	dep_svc->has_been_checked = TRUE;
	result = check_service_dependencies(svc, EXECUTION_DEPENDENCY);
	ck_assert(result == DEPENDENCIES_OK);

	dep_svc->current_state = STATE_CRITICAL;
	result = check_service_dependencies(svc, EXECUTION_DEPENDENCY);
	ck_assert(result == DEPENDENCIES_FAILED);
}
END_TEST

Suite *
check_dependencies_suite(void)
{
	Suite *s = suite_create("Check dependencies");
	TCase *tc_deps = tcase_create("Check dependencies");
	tcase_add_checked_fixture(tc_deps, setup, teardown);

	tcase_add_test(tc_deps, host_execution_no_dependency);
	tcase_add_test(tc_deps, host_execution_dependency_pending);
	tcase_add_test(tc_deps, host_execution_dependency_down);

	tcase_add_test(tc_deps, service_execution_no_dependency);
	tcase_add_test(tc_deps, service_execution_dependency_pending);
	tcase_add_test(tc_deps, service_execution_dependency_critical);
	suite_add_tcase(s, tc_deps);

	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = check_dependencies_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
