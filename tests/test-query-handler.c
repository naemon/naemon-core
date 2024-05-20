#include <check.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib/libnaemon.h"
#include "naemon/globals.h"
#include "naemon/events.h"
#include "naemon/query-handler.c"

static void run_main_loop(time_t runtime)
{
	time_t s, n;
	n = s = time(NULL);

	while (((runtime - (n - s)) > 0)) {
		iobroker_poll(nagios_iobs, 250);
		n = time(NULL);
	}
}

START_TEST(common_case)
{
	int ret, sd;
	char buf[256 * 1024];

	/* fake daemon mode to reduce noice on the console */
	daemon_mode = TRUE;
	qh_socket_path = "/tmp/naemon.qh";

	ck_assert_msg(NULL != (nagios_iobs = iobroker_create()), "failed to initialize iobroker");
	ret = qh_init(qh_socket_path);
	ck_assert_int_eq(OK, ret);
	registered_commands_init(200);
	register_core_commands();
	init_objects_service(0);

	sd = nsock_unix(qh_socket_path, NSOCK_TCP | NSOCK_CONNECT);
	ck_assert_msg(sd > 0, "failed to open client connection");
	ret = nsock_printf_nul(sd, "help");
	ck_assert_msg(ret > 0, "failed to send query");
	run_main_loop(1);
	ret = read(sd, &buf, 1024);
	ck_assert_msg(ret > 0, "failed to read response");
	ck_assert_msg(strstr(buf, "show help for handler") != NULL, "failed to get help");
	close(sd);

	sd = nsock_unix(qh_socket_path, NSOCK_TCP | NSOCK_CONNECT);
	ck_assert_msg(sd > 0, "failed to open client connection");
	ret = nsock_printf_nul(sd, "command run [123456789] test");
	ck_assert_msg(ret > 0, "failed to send query");
	run_main_loop(1);
	ret = read(sd, &buf, 1024);
	ck_assert_msg(ret > 0, "failed to read response");
	ck_assert_msg(strstr(buf, "Unknown command 'test'") != NULL, "incorrect response");
	close(sd);

	sd = nsock_unix(qh_socket_path, NSOCK_TCP | NSOCK_CONNECT);
	ck_assert_msg(sd > 0, "failed to open client connection");
	ret = nsock_printf_nul(sd, "command run [123456789] PROCESS_SERVICE_CHECK_RESULT;monitor;some_service;0;output");
	ck_assert_msg(ret > 0, "failed to send query");
	run_main_loop(1);
	ret = read(sd, &buf, 1024);
	ck_assert_msg(ret > 0, "failed to read response");
	ck_assert_msg(strstr(buf, "Failed validation of service") != NULL, "incorrect response");
	close(sd);

	registered_commands_deinit();
	qh_deinit(qh_socket_path);
	iobroker_destroy(nagios_iobs, IOBROKER_CLOSE_SOCKETS);
	nagios_iobs = NULL;
}
END_TEST

Suite *
checks_suite(void)
{
	Suite *s = suite_create("QueryHandler");
	TCase *rot = tcase_create("Test Queries");
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
