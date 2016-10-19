#include <check.h>
#include "naemon/commands.h"
#include "naemon/nebmods.h"
#include "naemon/neberrors.h"
#include "naemon/globals.h"

int test_command_exec_count;
int test_command_id;

nebmodule test_neb_module;
int test_neb_exec_count;
int test_neb_exit_code;

struct external_command *test_command = NULL;

static int test_neb_callback(int type, void *ds) {
	test_neb_exec_count++;
	return test_neb_exit_code;
}

static int test_command_handler(const struct external_command *ext_command, time_t entry_time) {
	test_command_exec_count++;
	return OK;
}

static void setup_neb(void) {
	int ret = OK;
	ret = neb_init_callback_list();
	ck_assert_int_eq(OK, ret);
	ret = neb_add_core_module(&test_neb_module);
	event_broker_options = -1; /* Allow all nebevents to be caught */

	registered_commands_init(20);

	test_command = command_create("TEST_COMMAND_EXEC", test_command_handler, "A test command for testing", NULL);
	test_command_id = command_register(test_command, -1);

	ret = neb_register_callback(NEBCALLBACK_EXTERNAL_COMMAND_DATA, test_neb_module.module_handle, 0,  test_neb_callback);
	ck_assert_int_eq(OK, ret);
}
static void teardown_neb(void) {
	registered_commands_deinit();

	neb_free_callback_list();
}

START_TEST( test_cb_process_external_command1_ok) {
	test_neb_exit_code = NEB_OK;

	test_command_exec_count = 0;
	test_neb_exec_count = 0;
	process_external_command1("[0] TEST_COMMAND_EXEC");
	ck_assert_int_eq(test_command_exec_count, 1);
	ck_assert_int_eq(test_neb_exec_count, 2); /* _START and _END */
}
END_TEST

START_TEST( test_cb_process_external_command1_cancel) {
	test_neb_exit_code = NEBERROR_CALLBACKCANCEL;

	test_command_exec_count = 0;
	test_neb_exec_count = 0;
	process_external_command1("[0] TEST_COMMAND_EXEC");
	ck_assert_int_eq(test_command_exec_count, 0); /* Stopped by _START */
	ck_assert_int_eq(test_neb_exec_count, 1); /* only _START */
}
END_TEST

START_TEST( test_cb_process_external_command1_override) {
	test_neb_exit_code = NEBERROR_CALLBACKOVERRIDE;

	test_command_exec_count = 0;
	test_neb_exec_count = 0;
	process_external_command1("[0] TEST_COMMAND_EXEC");
	ck_assert_int_eq(test_command_exec_count, 0); /* Stopped by _START */
	ck_assert_int_eq(test_neb_exec_count, 1); /* only _START */
}
END_TEST

START_TEST( test_cb_process_external_command2_ok) {
	test_neb_exit_code = NEB_OK;

	test_command_exec_count = 0;
	test_neb_exec_count = 0;
	process_external_command2(test_command_id, 0, NULL);
	ck_assert_int_eq(test_command_exec_count, 1);
	ck_assert_int_eq(test_neb_exec_count, 2); /* _START and _END */
}
END_TEST

START_TEST( test_cb_process_external_command2_cancel) {
	test_neb_exit_code = NEBERROR_CALLBACKCANCEL;

	test_command_exec_count = 0;
	test_neb_exec_count = 0;
	process_external_command2(test_command_id, 0, NULL);
	ck_assert_int_eq(test_command_exec_count, 0); /* Stopped by _START */
	ck_assert_int_eq(test_neb_exec_count, 1); /* only _START */
}
END_TEST

START_TEST( test_cb_process_external_command2_override) {
	test_neb_exit_code = NEBERROR_CALLBACKOVERRIDE;

	test_command_exec_count = 0;
	test_neb_exec_count = 0;
	process_external_command2(test_command_id, 0, NULL);
	ck_assert_int_eq(test_command_exec_count, 0); /* Stopped by _START */
	ck_assert_int_eq(test_neb_exec_count, 1); /* only _START */
}
END_TEST

Suite*
neb_cb_suite(void)
{
	Suite *s = suite_create("NEB Callbacks");
	TCase *tc_extcmd = tcase_create("External command");
	tcase_add_checked_fixture(tc_extcmd, setup_neb, teardown_neb);
	tcase_add_test(tc_extcmd, test_cb_process_external_command1_ok);
	tcase_add_test(tc_extcmd, test_cb_process_external_command1_cancel);
	tcase_add_test(tc_extcmd, test_cb_process_external_command1_override);
	tcase_add_test(tc_extcmd, test_cb_process_external_command2_ok);
	tcase_add_test(tc_extcmd, test_cb_process_external_command2_cancel);
	tcase_add_test(tc_extcmd, test_cb_process_external_command2_override);
	suite_add_tcase(s, tc_extcmd);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = neb_cb_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
