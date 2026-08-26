#include "naemon/configuration.h"
#include "naemon/utils.h"
#include "naemon/globals.h"
#include "naemon/defaults.h"
#include "naemon/nm_alloc.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <check.h>

char *cur_config_file = NULL;

static void init_configuration(void)
{
	int result;

	/* This is really not used, but needs to be defined */
	config_file = "(test config filename)";

	/* Just load defaults, but don't read main config file */
	result = reset_variables();
	ck_assert_int_eq(result, OK);

	/* Kick start with a clean config file we can write to with the test */
	cur_config_file = strdup("/tmp/nmtst.XXXXXX");
	close(mkstemp(cur_config_file));
	add_object_to_objectlist(&objcfg_files, cur_config_file);

}

static void free_configuration(void)
{

	/* Clean up the config file afterwards */
	if (cur_config_file) {
		unlink(cur_config_file);
	}

	/* To clean up object configuration */
	cleanup();
	cur_config_file = NULL;
}

static void object_def_start(const char *type)
{
	FILE *fp = fopen(cur_config_file, "a");
	fprintf(fp, "define %s {\n", type);
	fclose(fp);

}
static void object_def_var(const char *name, const char *var, ...)
{
	char tmpbuf[1024] = "";
	va_list args;
	FILE *fp;

	va_start(args, var);
	vsnprintf(tmpbuf, 1024, var, args);
	va_end(args);

	fp = fopen(cur_config_file, "a");
	fprintf(fp, "    %-30s %s\n", name, tmpbuf);
	fclose(fp);
}

static void object_def_end(void)
{
	FILE *fp = fopen(cur_config_file, "a");
	fprintf(fp, "}\n\n");
	fclose(fp);
}

/* Append a line without object_def_var() normalizing its whitespace. */
static void object_def_raw(const char *line)
{
	FILE *fp = fopen(cur_config_file, "a");
	fprintf(fp, "%s\n", line);
	fclose(fp);
}

static char captured_output[8192];

/* Parse while capturing console output in captured_output. */
static int read_all_object_data_capture(void)
{
	char tmpl[] = "/tmp/nmcapture.XXXXXX";
	int fd, saved_stdout, result;
	ssize_t nread;

	captured_output[0] = '\0';

	fd = mkstemp(tmpl);
	ck_assert_int_ne(fd, -1);

	fflush(stdout);
	saved_stdout = dup(STDOUT_FILENO);
	ck_assert_int_ne(saved_stdout, -1);
	ck_assert_int_ne(dup2(fd, STDOUT_FILENO), -1);

	result = read_all_object_data("(test config filename)");

	fflush(stdout);
	ck_assert_int_ne(dup2(saved_stdout, STDOUT_FILENO), -1);
	close(saved_stdout);

	ck_assert_msg(lseek(fd, 0, SEEK_SET) != (off_t)-1, "lseek() failed");
	nread = read(fd, captured_output, sizeof(captured_output) - 1);
	ck_assert_msg(nread >= 0, "read() failed");
	captured_output[nread > 0 ? nread : 0] = '\0';
	close(fd);
	unlink(tmpl);

	return result;
}

#define ck_assert_output_contains(needle) \
	ck_assert_msg(strstr(captured_output, (needle)) != NULL, \
	              "expected output to contain '%s', got:\n%s", (needle), captured_output)

#define ck_assert_output_lacks(needle) \
	ck_assert_msg(strstr(captured_output, (needle)) == NULL, \
	              "expected output not to contain '%s', got:\n%s", (needle), captured_output)

/* Parse a minimal host with one directive under test. */
static int parse_host_with(const char *directive, const char *value)
{
	object_def_start("host");
	object_def_var("host_name", "test_host");
	object_def_var("address", "127.0.0.1");
	object_def_var("max_check_attempts", "1");
	object_def_var(directive, "%s", value);
	object_def_end();

	return read_all_object_data_capture();
}

/**
 * host services should override hostgroup services, always. Not just for one
 * or a few hosts.
 *
 * This test verifies that each host, which has a local service my_svc, uses
 * that service instead of the host group defined one. It uses the required
 * parameter max_check_attempts to identify if it's a local or hostgroup service
 *
 * Since a problem has occurred with not all, but just some, services resolved
 * the inheritance correctly, the test uses at least 5 hosts with locally
 * defined services. (The problem that occured made one host resolve correctly,
 * other hosts used the hostgroup service)
 *
 * (tracked by op5 JIRA ticket MON-8000, )
 */
START_TEST(test_hostgroup_service_host_override)
{
	int result;
	int count;
	int i;

	host *hst;

	object_def_start("command");
	object_def_var("command_name", "cmd");
	object_def_var("command_line", "cmd");
	object_def_end();

	object_def_start("hostgroup");
	object_def_var("hostgroup_name", "my_hg");
	object_def_var("alias", "my_hg_alias");
	object_def_end();

	object_def_start("service");
	object_def_var("hostgroup_name", "my_hg");
	object_def_var("service_description", "my_svc");
	object_def_var("max_check_attempts", "17");
	object_def_var("check_command", "cmd");
	object_def_end();

	object_def_start("host");
	object_def_var("host_name", "my_host_nosvc");
	object_def_var("address", "127.0.0.1");
	object_def_var("max_check_attempts", "1");
	object_def_var("hostgroups", "my_hg");
	object_def_end();

	for (i = 0; i < 5; i++) {
		object_def_start("host");
		object_def_var("host_name", "my_host_%d", i);
		object_def_var("address", "127.0.0.1");
		object_def_var("max_check_attempts", "1");
		object_def_var("hostgroups", "my_hg");
		object_def_end();

		object_def_start("service");
		object_def_var("host_name", "my_host_%d", i);
		object_def_var("service_description", "my_svc");
		object_def_var("max_check_attempts", "77"); // We override this for test
		object_def_var("check_command", "cmd");
		object_def_end();
	}

	result = read_all_object_data("(test config filename)");
	ck_assert_int_eq(result, OK);

	count = 0;

	for (hst = host_list; hst != NULL; hst = hst->next) {
		/* Verify that each host has one, and only one, service */
		ck_assert(hst->services != NULL);
		ck_assert(hst->services->next == NULL);

		if (0 == strcmp(hst->name, "my_host_nosvc")) {
			/* Our host with inherited service */
			ck_assert_msg(hst->services->service_ptr->max_attempts == 17, "max_attempts == %d (expected 17) for service on host %s", hst->services->service_ptr->max_attempts, hst->name);
		} else {
			/* Our host with overridden service */
			ck_assert_msg(hst->services->service_ptr->max_attempts == 77, "max_attempts == %d (expected 77) for service on host %s", hst->services->service_ptr->max_attempts, hst->name);
		}

		count++;
	}

	ck_assert_int_eq(count, 5 + 1);
	unlink(cur_config_file);
}
END_TEST

/******************************************************************************
 * Boolean directives use the first digit and warn about trailing characters.
 *****************************************************************************/

START_TEST(test_register_zero_with_trailing_char_warned)
{
	ck_assert_int_eq(parse_host_with("register", "0v"), OK);
	ck_assert(host_list == NULL);
	ck_assert_output_contains("Warning:");
	ck_assert_output_contains("'0v'");
	ck_assert_output_contains("'register'");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_ten_warned)
{
	ck_assert_int_eq(parse_host_with("register", "10"), OK);
	ck_assert(host_list != NULL);
	ck_assert_output_contains("Warning:");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_one_with_trailing_char_warned)
{
	ck_assert_int_eq(parse_host_with("register", "1x"), OK);
	ck_assert(host_list != NULL);
	ck_assert_output_contains("Warning:");
	unlink(cur_config_file);
}
END_TEST

/* Values without a leading boolean digit remain errors. */
START_TEST(test_register_leading_char_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "v0"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_double_zero_warned)
{
	ck_assert_int_eq(parse_host_with("register", "00"), OK);
	ck_assert(host_list == NULL);
	ck_assert_output_contains("Warning:");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_zero_accepted)
{
	ck_assert_int_eq(parse_host_with("register", "0"), OK);
	ck_assert_output_lacks("trailing characters");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_one_accepted)
{
	ck_assert_int_eq(parse_host_with("register", "1"), OK);
	ck_assert_output_lacks("trailing characters");
	unlink(cur_config_file);
}
END_TEST

/* All boolean directives share this behavior. */
START_TEST(test_notifications_enabled_trailing_char_warned)
{
	ck_assert_int_eq(parse_host_with("notifications_enabled", "0v"), OK);
	ck_assert(host_list != NULL);
	ck_assert_int_eq(host_list->notifications_enabled, FALSE);
	ck_assert_output_contains("Warning:");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_contact_notification_bools_accepted)
{
	object_def_start("timeperiod");
	object_def_var("timeperiod_name", "none");
	object_def_var("alias", "Nothing");
	object_def_end();

	object_def_start("contact");
	object_def_var("contact_name", "test_contact");
	object_def_var("host_notifications_enabled", "0");
	object_def_var("service_notifications_enabled", "1");
	object_def_var("host_notification_period", "none");
	object_def_var("service_notification_period", "none");
	object_def_end();

	ck_assert_int_eq(read_all_object_data_capture(), OK);
	unlink(cur_config_file);
}
END_TEST

/* Word forms remain invalid. */
START_TEST(test_register_true_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "true"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_yes_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "yes"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_on_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "on"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_without_value_rejected)
{
	object_def_start("host");
	object_def_var("host_name", "test_host");
	object_def_var("address", "127.0.0.1");
	object_def_var("max_check_attempts", "1");
	object_def_raw("    register");
	object_def_end();

	ck_assert_int_eq(read_all_object_data_capture(), ERROR);
	unlink(cur_config_file);
}
END_TEST

/******************************************************************************
 * Surrounding whitespace is trimmed before parsing.
 *****************************************************************************/

START_TEST(test_value_tab_separated_accepted)
{
	object_def_start("host");
	object_def_var("host_name", "test_host");
	object_def_var("address", "127.0.0.1");
	object_def_var("max_check_attempts", "1");
	object_def_raw("\tregister\t0");
	object_def_end();

	ck_assert_int_eq(read_all_object_data_capture(), OK);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_value_trailing_whitespace_accepted)
{
	object_def_start("host");
	object_def_var("host_name", "test_host");
	object_def_var("address", "127.0.0.1");
	object_def_var("max_check_attempts", "1");
	object_def_raw("    register     0   \t  ");
	object_def_end();

	ck_assert_int_eq(read_all_object_data_capture(), OK);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_value_trailing_comment_accepted)
{
	object_def_start("host");
	object_def_var("host_name", "test_host");
	object_def_var("address", "127.0.0.1");
	object_def_var("max_check_attempts", "1");
	object_def_raw("    register 0 ; registered as a template");
	object_def_end();

	ck_assert_int_eq(read_all_object_data_capture(), OK);
	unlink(cur_config_file);
}
END_TEST

/******************************************************************************
 * Warnings name the directive as written, including aliases.
 *****************************************************************************/

START_TEST(test_warning_names_register_not_member)
{
	ck_assert_int_eq(parse_host_with("register", "0v"), OK);
	ck_assert_output_contains("'register'");
	ck_assert_output_lacks("register_object");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_warning_quotes_value)
{
	ck_assert_int_eq(parse_host_with("register", "0v"), OK);
	ck_assert_output_contains("'0v'");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_warning_names_alias_as_written)
{
	ck_assert_int_eq(parse_host_with("checks_enabled", "0v"), OK);
	ck_assert_output_contains("'checks_enabled'");
	ck_assert_output_lacks("active_checks_enabled");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_warning_names_canonical_alias_as_written)
{
	ck_assert_int_eq(parse_host_with("active_checks_enabled", "0v"), OK);
	ck_assert_output_contains("'active_checks_enabled'");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_warning_names_obsess_over_host_as_written)
{
	ck_assert_int_eq(parse_host_with("obsess_over_host", "0v"), OK);
	ck_assert_output_contains("'obsess_over_host'");
	ck_assert_output_lacks("'obsess'");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_warning_names_obsess_as_written)
{
	ck_assert_int_eq(parse_host_with("obsess", "0v"), OK);
	ck_assert_output_contains("'obsess'");
	unlink(cur_config_file);
}
END_TEST

Suite *obj_config_parse_suite(void)
{
	Suite *s = suite_create("Object config parse");
	TCase *parse = tcase_create("Parse configuration");
	tcase_add_checked_fixture(parse, init_configuration, free_configuration);

	tcase_add_test(parse, test_hostgroup_service_host_override);

	tcase_add_test(parse, test_register_zero_with_trailing_char_warned);
	tcase_add_test(parse, test_register_ten_warned);
	tcase_add_test(parse, test_register_one_with_trailing_char_warned);
	tcase_add_test(parse, test_register_leading_char_rejected);
	tcase_add_test(parse, test_register_double_zero_warned);
	tcase_add_test(parse, test_register_zero_accepted);
	tcase_add_test(parse, test_register_one_accepted);
	tcase_add_test(parse, test_notifications_enabled_trailing_char_warned);
	tcase_add_test(parse, test_contact_notification_bools_accepted);
	tcase_add_test(parse, test_register_true_rejected);
	tcase_add_test(parse, test_register_yes_rejected);
	tcase_add_test(parse, test_register_on_rejected);
	tcase_add_test(parse, test_register_without_value_rejected);

	tcase_add_test(parse, test_value_tab_separated_accepted);
	tcase_add_test(parse, test_value_trailing_whitespace_accepted);
	tcase_add_test(parse, test_value_trailing_comment_accepted);

	tcase_add_test(parse, test_warning_names_register_not_member);
	tcase_add_test(parse, test_warning_quotes_value);
	tcase_add_test(parse, test_warning_names_alias_as_written);
	tcase_add_test(parse, test_warning_names_canonical_alias_as_written);
	tcase_add_test(parse, test_warning_names_obsess_over_host_as_written);
	tcase_add_test(parse, test_warning_names_obsess_as_written);

	suite_add_tcase(s, parse);
	return s;
}

int main(void)
{
	int number_failed = 0;
	SRunner *sr = srunner_create(obj_config_parse_suite());
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
