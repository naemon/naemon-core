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

/* Append a line verbatim. object_def_var() always separates name and value
 * with spaces, so tests that care about the exact whitespace between a
 * directive and its value - or about a directive with no value at all - need
 * to write the line themselves. */
static void object_def_raw(const char *line)
{
	FILE *fp = fopen(cur_config_file, "a");
	fprintf(fp, "%s\n", line);
	fclose(fp);
}

static char captured_output[8192];

/**
 * Run read_all_object_data() with stdout redirected to a temporary file, so
 * that the text of any error can be asserted on. nm_log() reaches the console
 * through write_to_console(), which prints to stdout while daemon_mode is
 * FALSE - the case throughout these tests.
 *
 * The captured text is left in captured_output.
 */
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
	dup2(fd, STDOUT_FILENO);

	result = read_all_object_data("(test config filename)");

	fflush(stdout);
	dup2(saved_stdout, STDOUT_FILENO);
	close(saved_stdout);

	lseek(fd, 0, SEEK_SET);
	nread = read(fd, captured_output, sizeof(captured_output) - 1);
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

/**
 * Define a single host carrying one extra directive, then parse. A registered
 * host needs host_name, address and max_check_attempts; the directive under
 * test is added on top of those.
 */
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
 * Boolean object directives accept only the exact values 0 and 1.
 *
 * The parser used to look at the first character of the value only, so values
 * such as "0v" or "10" were silently accepted - "0v" quietly turning into
 * register 0, leaving the object unmonitored without any warning.
 *****************************************************************************/

START_TEST(test_register_trailing_char_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "0v"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_ten_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "10"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_one_trailing_char_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "1x"), ERROR);
	unlink(cur_config_file);
}
END_TEST

/* Already rejected before full-value matching was introduced; kept so that a
 * future change cannot start accepting it. */
START_TEST(test_register_leading_char_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "v0"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_double_zero_rejected)
{
	ck_assert_int_eq(parse_host_with("register", "00"), ERROR);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_zero_accepted)
{
	ck_assert_int_eq(parse_host_with("register", "0"), OK);
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_register_one_accepted)
{
	ck_assert_int_eq(parse_host_with("register", "1"), OK);
	unlink(cur_config_file);
}
END_TEST

/* The fix lives in one macro shared by every boolean directive, so a directive
 * other than register must behave identically. */
START_TEST(test_notifications_enabled_trailing_char_rejected)
{
	ck_assert_int_eq(parse_host_with("notifications_enabled", "0v"), ERROR);
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

/* The Nagios documentation defines these directives as 0/1 only - word forms
 * are not an accepted spelling. */
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

/* Unchanged behaviour, pinned so it stays that way. */
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
 * Whitespace around the value is not part of the value.
 *
 * Matching the value in full is only safe because the caller has already run
 * it through trim(). These tests fail loudly if that ever stops being true.
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
 * The error names the directive exactly as written in the config file.
 *
 * Several directives do not share a name with the struct member they set, so
 * reporting the member would name something the user never wrote:
 *   register            -> register_object
 *   checks_enabled      -> active_checks_enabled
 *   obsess_over_host    -> obsess
 *****************************************************************************/

START_TEST(test_error_names_register_not_member)
{
	ck_assert_int_eq(parse_host_with("register", "0v"), ERROR);
	ck_assert_output_contains("'register'");
	ck_assert_output_lacks("register_object");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_error_quotes_rejected_value)
{
	ck_assert_int_eq(parse_host_with("register", "0v"), ERROR);
	ck_assert_output_contains("'0v'");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_error_names_alias_as_written)
{
	ck_assert_int_eq(parse_host_with("checks_enabled", "0v"), ERROR);
	ck_assert_output_contains("'checks_enabled'");
	ck_assert_output_lacks("active_checks_enabled");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_error_names_canonical_alias_as_written)
{
	ck_assert_int_eq(parse_host_with("active_checks_enabled", "0v"), ERROR);
	ck_assert_output_contains("'active_checks_enabled'");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_error_names_obsess_over_host_as_written)
{
	ck_assert_int_eq(parse_host_with("obsess_over_host", "0v"), ERROR);
	ck_assert_output_contains("'obsess_over_host'");
	ck_assert_output_lacks("'obsess'");
	unlink(cur_config_file);
}
END_TEST

START_TEST(test_error_names_obsess_as_written)
{
	ck_assert_int_eq(parse_host_with("obsess", "0v"), ERROR);
	ck_assert_output_contains("'obsess'");
	unlink(cur_config_file);
}
END_TEST

/* The file and line come from the caller and must still accompany the value
 * error, so an operator can find the offending directive. */
START_TEST(test_error_reports_file_and_line)
{
	ck_assert_int_eq(parse_host_with("register", "0v"), ERROR);
	ck_assert_output_contains("Could not add object property in file");
	ck_assert_output_contains(cur_config_file);
	unlink(cur_config_file);
}
END_TEST

Suite *obj_config_parse_suite(void)
{
	Suite *s = suite_create("Object config parse");
	TCase *parse = tcase_create("Parse configuration");
	tcase_add_checked_fixture(parse, init_configuration, free_configuration);

	tcase_add_test(parse, test_hostgroup_service_host_override);

	tcase_add_test(parse, test_register_trailing_char_rejected);
	tcase_add_test(parse, test_register_ten_rejected);
	tcase_add_test(parse, test_register_one_trailing_char_rejected);
	tcase_add_test(parse, test_register_leading_char_rejected);
	tcase_add_test(parse, test_register_double_zero_rejected);
	tcase_add_test(parse, test_register_zero_accepted);
	tcase_add_test(parse, test_register_one_accepted);
	tcase_add_test(parse, test_notifications_enabled_trailing_char_rejected);
	tcase_add_test(parse, test_contact_notification_bools_accepted);
	tcase_add_test(parse, test_register_true_rejected);
	tcase_add_test(parse, test_register_yes_rejected);
	tcase_add_test(parse, test_register_on_rejected);
	tcase_add_test(parse, test_register_without_value_rejected);

	tcase_add_test(parse, test_value_tab_separated_accepted);
	tcase_add_test(parse, test_value_trailing_whitespace_accepted);
	tcase_add_test(parse, test_value_trailing_comment_accepted);

	tcase_add_test(parse, test_error_names_register_not_member);
	tcase_add_test(parse, test_error_quotes_rejected_value);
	tcase_add_test(parse, test_error_names_alias_as_written);
	tcase_add_test(parse, test_error_names_canonical_alias_as_written);
	tcase_add_test(parse, test_error_names_obsess_over_host_as_written);
	tcase_add_test(parse, test_error_names_obsess_as_written);
	tcase_add_test(parse, test_error_reports_file_and_line);

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
