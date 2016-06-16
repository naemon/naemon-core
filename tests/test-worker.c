#include "naemon/events.h"
#include "naemon/query-handler.h"
#include "naemon/globals.h"
#include "naemon/workers.h"
#include "naemon/commands.h"
#include "worker/worker.h"
#include "lib/libnaemon.h"
#include <check.h>

/*
 * A note about worker tests:
 * One requirement for us to be able to read the output of the
 * job is that it fflush()'es its output buffers before it exits.
 * The exit() function does that, by properly flushing and closing
 * all open filedescriptors, but the _exit() function does not.
 * fflush() means that the kernel will copy the buffer from the
 * process' output buffer to the connected pipe's input buffer,
 * which is the "real" requirement for us to be able to read them
 * (and for poll() and epoll() to be able to mark them as readable).
 *
 * That means that some of these tests may look a bit weird, but
 * that's because the output buffers of a program belong to the
 * process and are destroyed in the instant the kernel reclaims
 * the process (ie, as part of making it reapable).
 *
 * There is no way for us to read data that isn't flushed. There
 * never will be a way for us to read data that isn't flushed,
 * and we can't *ever* do anything at all about it.
 * The tests cover such things as correct error codes during
 * timeouts, wait_status for signals and most cases I could
 * think of when I wrote them though.
 */
#define EXITCODE(x) (x << 8) /* WEXITSTATUS(EXITCODE(x)) == x */
static struct wrk_test {
	char *command;
	char *expected_stdout;
	char *expected_stderr;

	int expected_wait_status;
	int expected_error_code;
	int timeout;
} test_jobs[] = {
	{
		"stdbuf -oL echo 'hello world'",
		"hello world\n",
		"",
		0, 0, 3
	},
	{
		"stdbuf -e0 /bin/sh -c 'echo \"this goes to stderr\" >&2'",
		"",
		"this goes to stderr\n",
		0, 0, 3
	},
	{
		"/bin/sh -c 'echo -n natt; sleep 3; echo -n hatt; sleep 3; echo -n kattegatt; exit 2'",
		"natthattkattegatt",
		"",
		EXITCODE(2), 0, 7
	},
	{
		"/bin/sh -c 'echo hoopla'",
		"hoopla\n",
		"",
		0, 0, 3
	},
	{
		"stdbuf -o0 -e0 /bin/sh -c 'echo -n nocrlf && echo -n lalala >&2 &&  exec 1>&- && exec 2>&- && exit 0'",
		"nocrlf",
		"lalala",
		0, 0, 3
	},
	{
		"/bin/sh -c 'sleep 50'",
		"",
		"",
		0, ETIME, 3,
	},
	{
		"stdbuf -o0 /bin/sh -c 'echo -n lalala && sleep 50'",
		"lalala",
		"",
		0, ETIME, 3,
	},
};
static int wrk_test_reaped;

void wrk_test_sighandler(int signo)
{
	printf("Caught signal %d. Aborting\n", signo);
	_exit(1);
}

void wrk_test_cb(struct wproc_result *wpres, void *data, int flags)
{
	struct wrk_test *t = (struct wrk_test *)data;

	wrk_test_reaped++;
	ck_assert_msg(wpres != NULL, "worker results should be non-NULL\n");
	ck_assert_msg(wpres->wait_status == t->expected_wait_status,
	   "wait_status: got %d, expected %d for command '%s'",
	   wpres->wait_status, t->expected_wait_status, t->command);
	ck_assert_int_eq(t->expected_error_code, wpres->error_code);
	ck_assert_msg(0 == strcmp(wpres->outstd, t->expected_stdout),
	   "STDOUT:\n###GOT\n%s\n##EXPECTED\n%s###STDOUT_END\ncommand: '%s'",
	   wpres->outstd, t->expected_stdout, t->command);
	ck_assert_msg(0 == strcmp(wpres->outerr, t->expected_stderr),
		"STDERR:\n###GOT\n%s###EXPECTED\n%s###STDERR_END\ncommand: '%s'",
		wpres->outerr, t->expected_stderr, t->command);

}

START_TEST(worker_test)
{
	int result;
	unsigned int i;
	time_t max_timeout = 0;
	int num_tests = sizeof(test_jobs) / sizeof(test_jobs[0]);

	timing_point("Starting worker tests\n");
	signal(SIGCHLD, wrk_test_sighandler);
	for (i = 0; i < sizeof(test_jobs) / sizeof(test_jobs[0]); i++) {
		struct wrk_test *j = &test_jobs[i];
		if (j->timeout > max_timeout)
			max_timeout = j->timeout;
		result = wproc_run_callback(j->command, j->timeout, wrk_test_cb, j, NULL);
		if (result) {
			fail("Failed to spawn job %d (%s). Aborting\n", i, j->command);
			exit(1);
		}
	}

	do {
		iobroker_poll(nagios_iobs, max_timeout * 1000);
	} while (wrk_test_reaped < num_tests);

	timing_point("Exiting normally\n");
}
END_TEST

/*
 * Make sure that the lifecycle of the command worker is deterministic w.r.t
 * its controlling functions:
 * E.g the command worker should be terminated on return of shutdown_command_file_worker()
 * and a PID should be available on return of launch_command_file_worker()
 */
START_TEST(command_worker_launch_shutdown_test)
{
	pid_t worker_pid;
	ck_assert_int_eq(0, command_worker_get_pid());
	ck_assert_int_eq(0, launch_command_file_worker());
	worker_pid = command_worker_get_pid();
	ck_assert_int_ne(0, worker_pid);
	ck_assert_int_eq(0, shutdown_command_file_worker());
	ck_assert_int_eq(-1, kill(worker_pid, 0));
	ck_assert_int_eq(0, command_worker_get_pid());
}
END_TEST

void init_iobroker(void) {
	ck_assert(NULL != (nagios_iobs = iobroker_create()));
}

void deinit_iobroker(void) {
	iobroker_destroy(nagios_iobs, IOBROKER_CLOSE_SOCKETS);
}

void wrk_test_init(void)
{
	time_t start;
	log_file = "/dev/stdout";
	init_iobroker();
	enable_timing_point = 1;
	qh_socket_path = "/tmp/qh-socket";
	qh_init(qh_socket_path);

	init_workers(3);
	start = time(NULL);
	while (wproc_num_workers_online < wproc_num_workers_spawned && time(NULL) < start + 10) {
		iobroker_poll(nagios_iobs, 1000);
	}
	timing_point("%d/%d workers online\n", wproc_num_workers_online, wproc_num_workers_spawned);
	ck_assert_int_eq(wproc_num_workers_spawned, wproc_num_workers_online);
}

void wrk_test_deinit(void)
{
	free_worker_memory(WPROC_FORCE);
	deinit_iobroker();
}

Suite *worker_suite(void)
{
	Suite *s = suite_create("worker tests");
	TCase *tc_worker_output = tcase_create("worker output reaping tests");
	TCase *tc_command_worker = tcase_create("command worker tests");
	tcase_add_checked_fixture(tc_worker_output, wrk_test_init, wrk_test_deinit);
	tcase_add_checked_fixture(tc_command_worker, init_iobroker, deinit_iobroker);
	tcase_add_test(tc_worker_output, worker_test);
	tcase_add_test(tc_command_worker, command_worker_launch_shutdown_test);
	suite_add_tcase(s, tc_command_worker);
	suite_add_tcase(s, tc_worker_output);
	return s;
}

int main(int argc, char **argv)
{
	int failed;
	SRunner *sr = srunner_create(worker_suite());
	srunner_set_fork_status(sr, CK_NOFORK);

	/* warning: here there be globals
	 * this hack makes the spawn_core_worker() function
	 * believe that this test suite is the naemon binary,
	 * and run it with the --worker option, which we've "implemented"
	 * here... Yuck.
	 * */
	naemon_binary_path = argv[0];
	if (argc > 1) {
		if (!strcmp(argv[1], "--worker")) {
			return nm_core_worker(argv[2]);
		}
	}
	srunner_run_all(sr, CK_ENV);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return !failed ? EXIT_SUCCESS : EXIT_FAILURE;
}
