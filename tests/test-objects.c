/**
 * Extremely rudimentary object api tests
 * Basically, if the tests don't crash, we're good.
 */
#include <check.h>
#include <stdlib.h>
#include "naemon/objects.h"
#include "naemon/objects_host.h"
#include "naemon/objects_service.h"
#include "naemon/objects_timeperiod.h"
#include "naemon/objects_command.h"
#include "naemon/objects_hostgroup.h"
#include "naemon/objects_servicegroup.h"
#include "naemon/objects_contactgroup.h"

static struct host test_host = {
	.name = "t-host",
};
static struct service test_service = {
	.description = "description",
};
static struct command test_command = {
	.name = "t-command",
};
static struct timeperiod test_timeperiod = {
	.name = "t-timeperiod",
};
static struct hostgroup test_hostgroup = {
	.group_name = "t-hostgroup",
};
static struct servicegroup test_servicegroup = {
	.group_name = "t-servicegroup",
};
static struct contact test_contact = {
	.name = "t-contact",
};
static struct contactgroup test_contactgroup = {
	.group_name = "t-contactgroup",
};

#define TEST_FIND(obj) \
	do { \
		ck_assert_msg(find_##obj("t-" #obj) == &test_##obj, "find_" #obj "(t-" #obj ") == &test_"#obj); \
		ck_assert_msg(find_##obj(NULL) == NULL, "find_" #obj "(NULL) must yield NULL"); \
		ck_assert_msg(find_##obj("AN INVALID NAME") == NULL, "find_" #obj "(AN INVALID NAME) must yield NULL"); \
	} while (0)


START_TEST(test_lookups)
{
	TEST_FIND(host);
	ck_assert_msg(find_service(test_host.name, test_service.description) == &test_service, "find_service() valid input");
	ck_assert_msg(find_service(NULL, "t-description") == NULL, "find_service() invalid host");
	ck_assert_msg(find_service("t-host", NULL) == NULL, "find_service() invalid description");
	ck_assert_msg(find_service(NULL, NULL) == NULL, "find_service() invalid hostname and description");
	TEST_FIND(command);
	TEST_FIND(timeperiod);
	TEST_FIND(hostgroup);
	TEST_FIND(servicegroup);
	TEST_FIND(contactgroup);
}
END_TEST

#define TST_SETUP_OBJ(obj) \
	do { \
		init_objects_##obj(1); \
		ck_assert(0 == register_##obj(&test_##obj)); \
	} while (0)
static void setup_objects(void)
{
	test_service.host_name = test_host.name;
	TST_SETUP_OBJ(host);
	TST_SETUP_OBJ(hostgroup);
	TST_SETUP_OBJ(service);
	TST_SETUP_OBJ(servicegroup);
	TST_SETUP_OBJ(command);
	TST_SETUP_OBJ(timeperiod);
	TST_SETUP_OBJ(contact);
	TST_SETUP_OBJ(contactgroup);
}

static void teardown_objects(void)
{
}

static Suite *objects_suite(void)
{
	Suite *s = suite_create("Objects");
	TCase *tc = tcase_create("Objects");
	tcase_add_checked_fixture(tc, setup_objects, teardown_objects);
	tcase_add_test(tc, test_lookups);
	suite_add_tcase(s, tc);
	return s;
}

int main(int argc, char **argv)
{
	int number_failed = 0;
	Suite *s = objects_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return number_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
