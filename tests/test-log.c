#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <check.h>
#include "naemon/logging.c"


START_TEST(common_case)
{
	int fd, ret;
	size_t len;
	char active_contents[1024], rotated_contents[1024];
	time_t rotate_time = 1234, log_ts1 = 5678, log_ts2 = 9012;
	char workdir[1024], *rotated_file;
	logging_options = -1;
	getcwd(workdir, 1024);
	asprintf(&rotated_file, "%s/old.log", workdir);
	asprintf(&log_file, "%s/active.log", workdir);

	// please fail if the files already exist:
	ck_assert_msg(access(log_file, F_OK) == -1,
			"Log file '%s' already exists - cowardly refusing to unlink it for you", log_file);
	ck_assert_msg(access(rotated_file, F_OK) == -1,
			"Log file '%s' already exists - cowardly refusing to unlink it for you", rotated_file);

	ret = write_to_log("Log information", -1, &log_ts1);
	ck_assert_int_eq(OK, ret);
	ret = rename(log_file, rotated_file);
	ck_assert_int_eq(0, ret);
	ret = rotate_log_file(rotate_time);
	ck_assert_int_eq(OK, ret);
	ret = write_to_log("New log information", -1, &log_ts2);
	ck_assert_int_eq(OK, ret);

	ck_assert_int_eq(0, access(log_file, R_OK));
	ck_assert_int_eq(0, access(rotated_file, R_OK));
	fd = open(log_file, O_RDONLY);
	len = read(fd, active_contents, 1024);
	active_contents[len] = '\0';
	close(fd);
	fd = open(rotated_file, O_RDONLY);
	len = read(fd, rotated_contents, 1024);
	rotated_contents[len] = '\0';
	close(fd);
	ck_assert_str_eq("[1234] LOG ROTATION: EXTERNAL\n[1234] LOG VERSION: 2.0\n[9012] New log information\n", active_contents);
	ck_assert_str_eq("[5678] Log information\n", rotated_contents);
	unlink(rotated_file);
	unlink(log_file);
}
END_TEST

Suite*
checks_suite(void)
{
	Suite *s = suite_create("Logs");
	TCase *rot = tcase_create("Handling log rotation");
	tcase_add_test(rot, common_case);
	suite_add_tcase(s, rot);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = checks_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
