#include <check.h>
#include "naemon/checks.h"

char *full_output;
char *short_output;
char *long_output;
char *perf_data;
char *output;

void setup (void) {
	short_output = NULL;
	long_output = NULL;
	perf_data = NULL;
	full_output = NULL;
	output = NULL;
}
void teardown (void) {
	free(output);
	free(short_output);
	free(long_output);
	free(perf_data);
}

START_TEST(one_line_no_perfdata)
{
	full_output = "TEST OK - just one line of output, no perfdata";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq(short_output, full_output);
	ck_assert(NULL == long_output);
	ck_assert(NULL == perf_data);

}
END_TEST

START_TEST(one_line_with_perfdata)
{
	full_output = "TEST WARNING - a line of output and | some=perfdata;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("TEST WARNING - a line of output and", short_output);
	ck_assert_str_eq("some=perfdata;", perf_data);
	ck_assert(NULL == long_output);
}
END_TEST

START_TEST(multiple_line_output_no_perfdata)
{
	full_output = "TEST WARNING - first a line of output\n"
				  "and then some more output on another line";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("TEST WARNING - first a line of output", short_output);
	ck_assert_str_eq("and then some more output on another line", long_output);
	ck_assert(NULL == perf_data);
}
END_TEST

START_TEST(multiple_line_output_and_multiple_line_perfdata)
{
	full_output = "TEST OK - a line of output and | some=perfdata;\n"
		"Here's some additional\n"
		"LONG output\n"
		"which suddenly becomes | more=perfdata;\n"
		"on=several;lines;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("TEST OK - a line of output and", short_output);
	ck_assert_str_eq("Here's some additional\nLONG output\nwhich suddenly becomes ", long_output );
	ck_assert_str_eq("some=perfdata; more=perfdata; on=several;lines;", perf_data);

}
END_TEST

START_TEST(multiple_line_output_and_perfdata_but_not_on_first_line)
{
	full_output = "TEST CRITICAL - Oh my\n"
				  "Here's a second line of output and | some=perfdata;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("TEST CRITICAL - Oh my", short_output);
	ck_assert_str_eq("some=perfdata;", perf_data);
	ck_assert_str_eq("Here's a second line of output and ", long_output);
}
END_TEST

START_TEST(one_line_output_and_perfdata_but_not_on_first_line)
{
	full_output = "TEST CRITICAL - Oh my\n| some=perfdata;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("TEST CRITICAL - Oh my", short_output);
	ck_assert(NULL == long_output);
	ck_assert_str_eq("some=perfdata;", perf_data);
}
END_TEST

START_TEST(perfdata_only)
{
	full_output = "| some=perfdata;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("", short_output);
	ck_assert(NULL == long_output);
	ck_assert_str_eq("some=perfdata;", perf_data);
}
END_TEST


START_TEST(multiline_perfdata_only)
{
	full_output = "| some=perfdata;\n"
				  "|and=more;perfdata;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("", short_output);
	ck_assert(NULL == long_output);
	ck_assert_str_eq("some=perfdata; and=more;perfdata;", perf_data);
}
END_TEST

START_TEST(no_plugin_output_at_all)
{
	output = NULL;
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert(NULL == short_output);
	ck_assert(NULL == long_output);
	ck_assert(NULL == perf_data);

}
END_TEST

START_TEST(empty_plugin_output)
{
	output = strdup("");
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert(NULL == short_output);
	ck_assert(NULL == long_output);
	ck_assert(NULL == perf_data);
}
END_TEST

START_TEST(no_plugin_output_on_first_line)
{
	full_output = "\n|some=perfdata;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, FALSE, FALSE);
	ck_assert_str_eq("", short_output);
	ck_assert(NULL == long_output);
	ck_assert_str_eq("some=perfdata;", perf_data);

}
END_TEST

START_TEST(escape_multiple_line_output_and_multiple_line_perfdata)
{
	full_output = "TEST OK - a line of output and | some=perfdata;\n"
		"Here's some additional\n"
		"LONG output\n"
		"which suddenly becomes | more=perfdata;\n"
		"on=several;lines;";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, TRUE, FALSE);
	ck_assert_str_eq("TEST OK - a line of output and", short_output);
	ck_assert_str_eq("Here's some additional\\nLONG output\\nwhich suddenly becomes ", long_output );
	ck_assert_str_eq("some=perfdata; more=perfdata; on=several;lines;", perf_data);

}
END_TEST

START_TEST(unescape_multiple_line_output_and_multiple_line_perfdata)
{
	full_output = "TEST CRITICAL - Plugin short output|perfdata1; perfdata2;\\n"
		"Some long plugin output line\\n"
		"Some other line with escaped \\\\ backslash\\n"
		"last one| perfdata3";
	output = strdup(full_output);
	parse_check_output(output, &short_output, &long_output, &perf_data, TRUE, TRUE);
	ck_assert_str_eq("TEST CRITICAL - Plugin short output", short_output);
	ck_assert_str_eq("perfdata1; perfdata2; perfdata3", perf_data);
	ck_assert_str_eq("Some long plugin output line\\nSome other line with escaped \\\\ backslash\\nlast one", long_output);

}
END_TEST

Suite*
checks_suite(void)
{
	Suite *s = suite_create("Checks");
	TCase *tc_output = tcase_create("Output parsing");
	tcase_add_unchecked_fixture(tc_output, setup, teardown);
	tcase_add_test(tc_output, one_line_no_perfdata);
	tcase_add_test(tc_output, one_line_with_perfdata);
	tcase_add_test(tc_output, multiple_line_output_no_perfdata);
	tcase_add_test(tc_output, multiple_line_output_and_multiple_line_perfdata);
	tcase_add_test(tc_output, multiple_line_output_and_perfdata_but_not_on_first_line);
	tcase_add_test(tc_output, one_line_output_and_perfdata_but_not_on_first_line);
	tcase_add_test(tc_output, perfdata_only);
	tcase_add_test(tc_output, multiline_perfdata_only);
	tcase_add_test(tc_output, no_plugin_output_on_first_line);
	tcase_add_test(tc_output, no_plugin_output_at_all);
	tcase_add_test(tc_output, empty_plugin_output);
	tcase_add_test(tc_output, escape_multiple_line_output_and_multiple_line_perfdata);
	tcase_add_test(tc_output, unescape_multiple_line_output_and_multiple_line_perfdata);
	suite_add_tcase(s, tc_output);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = checks_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
