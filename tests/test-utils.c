#include <check.h>
#include "naemon/utils.h"


START_TEST(my_strtok_null_buffer)
{
	char *result = NULL;
	result = my_strtok(NULL, "X");
	ck_assert(NULL == result);
}
END_TEST

Suite*
utils_suite(void)
{
	Suite *s = suite_create("Utilities");
	TCase *tc_my_strtok = tcase_create("my_strtok");
	tcase_add_test(tc_my_strtok, my_strtok_null_buffer);
	suite_add_tcase(s, tc_my_strtok);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = utils_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
