#include <naemon/commands.c>

#include <check.h>

static int test_test_command_handler(const struct external_command *ext_command, time_t entry_time) {
	return 0;
}


static int validate_int_not_seventeen(void *value)
{
	return (*(int*)value) == 17 ? 0 : 1;
}


static void test_load_commands(void) {
	struct external_command *ext_command;
	check_external_commands = FALSE;
	registered_commands_init(20);

	ext_command = command_create("TEST_COMMAND", test_test_command_handler, "This is a description for a command named TEST_COMMAND", NULL);
	command_argument_add(ext_command, "something_bool", BOOL, NULL, NULL);
	command_argument_add(ext_command, "author", INTEGER, NULL, validate_int_not_seventeen);
	command_argument_add(ext_command, "comment", STRING, NULL, NULL);
	command_register(ext_command, -1);
}
static void test_unload_commands(void) {
	registered_commands_deinit();
}

START_TEST( kv_command_parsing) {
	struct external_command *extcmd;
	GError *error = NULL;
	void *argval_ptr;

	extcmd = command_parse("command=TEST_COMMAND;author=1;comment=boll\\;kaka;boll", COMMAND_SYNTAX_KV, &error);
	ck_assert(g_error_matches(error, NM_COMMAND_ERROR, CMD_ERROR_MALFORMED_COMMAND));
	ck_assert(extcmd == NULL);
	g_clear_error(&error);

	extcmd = command_parse("command=TEST_COMMAND;something_bool=1;author=1;comment=boll\\;kaka", COMMAND_SYNTAX_KV, &error);
	ck_assert(error == NULL);
	ck_assert(extcmd != NULL);
	ck_assert_str_eq(extcmd->name, "TEST_COMMAND");
	ck_assert(extcmd->handler == test_test_command_handler);
	g_clear_error(&error);

	argval_ptr = command_argument_get_value(extcmd,"something_bool");
	ck_assert(argval_ptr != NULL);
	ck_assert_int_eq(*(int*)argval_ptr, 1);

	argval_ptr = command_argument_get_value(extcmd,"author");
	ck_assert(argval_ptr != NULL);
	ck_assert_int_eq(*(int*)argval_ptr, 1);

	argval_ptr = command_argument_get_value(extcmd,"comment");
	ck_assert(argval_ptr != NULL);
	ck_assert_str_eq((char*)argval_ptr, "boll;kaka");

	command_destroy(extcmd);
}
END_TEST

START_TEST( kv_command_undefined_command_name) {
	struct external_command *extcmd;
	GError *error = NULL;

	extcmd = command_parse("somethingnotcommand=TEST_COMMAND;comment=boll\\;kaka", COMMAND_SYNTAX_KV, &error);
	ck_assert(g_error_matches(error, NM_COMMAND_ERROR, CMD_ERROR_UNKNOWN_COMMAND));
	ck_assert(extcmd == NULL);
	g_clear_error(&error);
}
END_TEST

START_TEST( kv_command_undefined_variable) {
	struct external_command *extcmd;
	GError *error = NULL;

	extcmd = command_parse("command=TEST_COMMAND;comment=boll\\;kaka", COMMAND_SYNTAX_KV, &error);
	ck_assert(g_error_matches(error, NM_COMMAND_ERROR, CMD_ERROR_PARSE_MISSING_ARG));
	ck_assert(extcmd == NULL);
	g_clear_error(&error);
}
END_TEST

START_TEST( kv_command_variable_validator) {
	struct external_command *extcmd;
	GError *error = NULL;

	extcmd = command_parse("command=TEST_COMMAND;something_bool=1;author=17;comment=boll", COMMAND_SYNTAX_KV, &error);
	ck_assert(g_error_matches(error, NM_COMMAND_ERROR, CMD_ERROR_VALIDATION_FAILURE));
	ck_assert(extcmd == NULL);
	g_clear_error(&error);
}
END_TEST

START_TEST( kv_command_raw_arguments_set) {
	struct external_command *extcmd;
	GError *error = NULL;
	const char *raw_args;

	extcmd = command_parse("command=TEST_COMMAND;something_bool=1;author=2;comment=kaka", COMMAND_SYNTAX_KV, &error);
	ck_assert(error == NULL);
	ck_assert(extcmd != NULL);

	raw_args = command_raw_arguments(extcmd);
	ck_assert(raw_args != NULL);
	ck_assert_str_eq(raw_args, "1;2;kaka");

	command_destroy(extcmd);
}
END_TEST

Suite *kv_command_suite(void) {
	Suite *s = suite_create("Key/Value-command");

	TCase *tc;
	tc = tcase_create("Key/Value-command");
	tcase_add_checked_fixture(tc, test_load_commands, test_unload_commands);
	tcase_add_test(tc, kv_command_parsing);
	tcase_add_test(tc, kv_command_undefined_command_name);
	tcase_add_test(tc, kv_command_undefined_variable);
	tcase_add_test(tc, kv_command_variable_validator);
	tcase_add_test(tc, kv_command_raw_arguments_set);
	suite_add_tcase(s, tc);

	return s;
}

int main(void) {
	int number_failed = 0;
	Suite *s = kv_command_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
