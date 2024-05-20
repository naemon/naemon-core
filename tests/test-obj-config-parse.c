#include "naemon/configuration.h"
#include "naemon/utils.h"
#include "naemon/globals.h"
#include "naemon/defaults.h"
#include "naemon/nm_alloc.h"

#include <stdio.h>
#include <stdarg.h>
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

Suite *obj_config_parse_suite(void)
{
	Suite *s = suite_create("Object config parse");
	TCase *parse = tcase_create("Parse configuration");
	tcase_add_checked_fixture(parse, init_configuration, free_configuration);

	tcase_add_test(parse, test_hostgroup_service_host_override);

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
