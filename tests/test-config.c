#include "naemon/configuration.h"
#include "naemon/utils.h"
#include "naemon/globals.h"
#include "naemon/defaults.h"
#include "naemon/nm_alloc.h"

#include <check.h>

/**
 * Apparently, a lot of ways to specify services are broken and we didn't
 * notice. Gogo regression tests!
 */
START_TEST(services)
{
	int res, hits, s5_hits = 0;
	service *s;
	objcfg_files = NULL;
	objcfg_dirs = NULL;
	res = reset_variables();
	ck_assert_int_eq(OK, res);
	config_file_dir = nspath_absolute_dirname(TESTDIR "services/naemon.cfg", NULL);
	res = read_main_config_file(TESTDIR "services/naemon.cfg");
	ck_assert_int_eq(OK, res);
	res = read_all_object_data(TESTDIR "services/naemon.cfg");
	ck_assert_int_eq(OK, res);
	for (s = service_list, hits = 0; s; s = s->next, hits++) {
		if (!strcmp(s->description, "service3")) {
			ck_assert_str_eq("from_template", s->display_name);
		}
		if (!strcmp(s->description, "service5")) {
			s5_hits++;
			ck_assert_msg(!strncmp("host", s->host_name, 4), "Only the host* hosts should match");
		}
	}
	ck_assert_int_eq(2, s5_hits);
	ck_assert_int_eq(5, hits);
	nm_free(config_file_dir);
	cleanup();
}
END_TEST

/**
 * Check that recursive objects don't too weird loops - a recursive object
 * should be included once, but not twice.
 */
START_TEST(recursive)
{
	int res, hits;
	host *h;
	objcfg_files = NULL;
	objcfg_dirs = NULL;
	res = reset_variables();
	ck_assert_int_eq(OK, res);
	config_file_dir = nspath_absolute_dirname(TESTDIR "recursive/naemon.cfg", NULL);
	res = read_main_config_file(TESTDIR "recursive/naemon.cfg");
	ck_assert_int_eq(OK, res);
	res = read_all_object_data(TESTDIR "recursive/naemon.cfg");
	ck_assert_int_eq(OK, res);
	for (h = host_list, hits = 0; h; h = h->next, hits++) {
		if (!strcmp(h->name, "host1")) {
			ck_assert_str_eq("from_template", h->alias);
			ck_assert_str_eq("", h->notes);
		} else if (!strcmp(h->name, "hosttemplate")) {
			ck_assert_str_eq("from_template", h->alias);
		} else {
			ck_abort_msg("Found unexpected host: %s", h->name);
		}
	}
	ck_assert_msg(hits == 2, "Expected 2 hosts, found %i", hits);
	nm_free(config_file_dir);
	cleanup();
}
END_TEST

START_TEST(main_include)
{
	int res;
	char *file_cfg = nspath_normalize(TESTDIR "inc/a_file.cfg");
	char *dir_cfg = nspath_normalize(TESTDIR "inc/a_dir");
	objcfg_files = NULL;
	objcfg_dirs = NULL;
	config_file_dir = nspath_absolute_dirname(TESTDIR "inc/naemon.cfg", NULL);
	res = read_main_config_file(TESTDIR "inc/naemon.cfg");
	ck_assert_int_eq(OK, res);
	ck_assert_int_eq(1448, event_handler_timeout);
	// leave files without .cfg suffix alone:
	ck_assert_int_eq(DEFAULT_HOST_CHECK_TIMEOUT, host_check_timeout);
	ck_assert_int_eq(1338, notification_timeout);
	ck_assert(NULL != objcfg_files);
	ck_assert_str_eq(file_cfg, objcfg_files->object_ptr);
	ck_assert(NULL != objcfg_dirs);
	ck_assert_str_eq(dir_cfg, objcfg_dirs->object_ptr);
	nm_free(file_cfg);
	nm_free(dir_cfg);
	nm_free(config_file_dir);
}
END_TEST

Suite *
config_suite(void)
{
	Suite *s = suite_create("Config");
	TCase *parse = tcase_create("Parse configuration");
	tcase_add_test(parse, recursive);
	tcase_add_test(parse, services);
	tcase_add_test(parse, main_include);
	suite_add_tcase(s, parse);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = config_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
