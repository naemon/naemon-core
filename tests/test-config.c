#include "naemon/configuration.h"
#include "naemon/utils.h"

#include <check.h>

START_TEST(recursive)
{
	int res, hits;
	host *h;
	res = reset_variables();
	ck_assert_int_eq(OK, res);
	res = read_main_config_file(SYSCONFDIR "recursive/naemon.cfg");
	ck_assert_int_eq(OK, res);
	res = read_all_object_data(SYSCONFDIR "recursive/naemon.cfg");
	ck_assert_int_eq(OK, res);
	for (h = host_list, hits=0; h; h = h->next, hits++) {
		if (!strcmp(h->name, "host1")) {
			ck_assert_str_eq("from_template", h->alias);
		} else if (!strcmp(h->name, "hosttemplate")) {
			ck_assert_str_eq("from_template", h->alias);
		} else {
			ck_abort_msg("Found unexpected host: %s", h->name);
		}
	}
	ck_assert_msg(hits == 2, "Expected 2 hosts, found %i", hits);
}
END_TEST

Suite*
config_suite(void)
{
	Suite *s = suite_create("Config");
	TCase *parse = tcase_create("Parse configuration");
	tcase_add_test(parse, recursive);
	suite_add_tcase(s, parse);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = config_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
