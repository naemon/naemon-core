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

	char *full_command;
	int expected_wait_status;
	int expected_error_code;
	int timeout;
} test_jobs[] = {
	{
		"print=\"hello world\n\" exit=0",
		"hello world\n",
		"",
		NULL, 0, 0, 3
	},
	{
		"eprint='this goes to stderr\n' exit=0",
		"",
		"this goes to stderr\n",
		NULL, 0, 0, 3
	},
	{
		"print='natt' sleep=3 print='hatt' sleep=3 print='kattegatt' exit=2",
		"natthattkattegatt",
		"",
		NULL, EXITCODE(2), 0, 7
	},
	{
		"print='hoopla\n' fflush=1 _exit=0",
		"hoopla\n",
		"",
		NULL, 0, 0, 3
	},
	{
		"print=nocrlf close=1 fflush=1 _exit=0",
		"",
		"",
		NULL, 0, 0, 3
	},
	{
		"print=nocrlf eprint=lalala fflush=1 close=1 fflush=2 close=2 _exit=0",
		"nocrlf",
		"lalala",
		NULL, 0, 0, 3
	},
	{
		"sleep=50",
		"",
		"",
		NULL, 0, ETIME, 3,
	},
	{
		"print='lalala' fflush=1 sleep=50",
		"lalala",
		"",
		NULL, 0, ETIME, 3,
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
	   "wait_status: got %d, expected %d",
	   wpres->wait_status, t->expected_wait_status);
	ck_assert_msg(wpres->error_code == t->expected_error_code,
	   "error_code: got %d, expected %d",
	   wpres->error_code, t->expected_error_code);
	ck_assert_msg(0 == strcmp(wpres->outstd, t->expected_stdout),
	   "STDOUT:\n###GOT\n%s\n##EXPECTED\n%s###STDOUT_END",
	   wpres->outstd, t->expected_stdout);
	ck_assert_msg(0 == strcmp(wpres->outerr, t->expected_stderr),
		"STDERR:\n###GOT\n%s###EXPECTED\n%s###STDERR_END",
		wpres->outerr, t->expected_stderr);

	free(t->full_command);
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
		result = asprintf(&j->full_command, "%s %s", naemon_binary_path, j->command);
		if (result < 0) {
			printf("Failed to create command line for job. Aborting\n");
			exit(1);
		}
		if (j->timeout > max_timeout)
			max_timeout = j->timeout;
		result = wproc_run_callback(j->full_command, j->timeout, wrk_test_cb, j, NULL);
		if (result) {
			fail("Failed to spawn job %d (%s). Aborting\n", i, j->full_command);
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

/*
 * This is a really stupid "plugin" with scriptable behaviour.
 * It accepts commands and executes them in-order, like so:
 * usleep=<int> : usleep()'s the given number of microseconds
 * sleep=<int>  : sleep()'s the given number of seconds
 * print=<str>  : prints the given string to stdout
 * eprint=<str> : prints the given string to stderr
 * fflush=<1|2> : fflush()'es <stdout|stderr>
 * close=<int>  : closes the given filedescriptor
 * exit=<int>   : exit()'s with the given code
 * _exit=<int>  : _exit()'s with the given code
 * signal=<int> : sends the given signal to itself
 * Commands that aren't understood are simply printed as-is.
 */
static int wrk_test_plugin(int argc, char **argv)
{
	int i;

	/*
	 * i = 0 is not a typo here. We only get called with leftover args
	 * from the main program invocation
	 */
	for (i = 0; i < argc; i++) {
		char *eq, *cmd;
		int value;

		cmd = argv[i];

		if (!(eq = strchr(cmd, '='))) {
			printf("%s", argv[i]);
			continue;
		}

		*eq = 0;
		value = atoi(eq + 1);
		if (!strcmp(cmd, "usleep")) {
			usleep(value);
		}
		else if (!strcmp(cmd, "sleep")) {
			sleep(value);
		}
		else if (!strcmp(cmd, "print")) {
			printf("%s", eq + 1);
		}
		else if (!strcmp(cmd, "eprint")) {
			fprintf(stderr, "%s", eq + 1);
		}
		else if (!strcmp(cmd, "close")) {
			close(value);
		}
		else if (!strcmp(cmd, "exit")) {
			exit(value);
		}
		else if (!strcmp(cmd, "_exit")) {
			_exit(value);
		}
		else if (!strcmp(cmd, "signal")) {
			kill(getpid(), value);
		}
		else if (!strcmp(cmd, "fflush")) {
			if (value == 1)
				fflush(stdout);
			else if (value == 2)
				fflush(stderr);
			else
				fflush(NULL);
		}
	}

	return 0;
}
void init_iobroker(void) {
	ck_assert(NULL != (nagios_iobs = iobroker_create()));
}

void deinit_iobroker(void) {
	iobroker_destroy(nagios_iobs, IOBROKER_CLOSE_SOCKETS);
}

void wrk_test_init(void)
{
	time_t start;
	printf("wrk_test_init\n");
	log_file = "/dev/stdout";
	timing_point("Initializing iobroker\n");
	init_iobroker();
	enable_timing_point = 1;

	timing_point("Initializing query handler socket\n");
	qh_socket_path = "/tmp/qh-socket";
	qh_init(qh_socket_path);

	timing_point("Launching workers\n");
	init_workers(3);
	start = time(NULL);
	while (wproc_num_workers_online < wproc_num_workers_spawned && time(NULL) < start + 45) {
		iobroker_poll(nagios_iobs, 1000);
	}
	timing_point("%d/%d workers online\n", wproc_num_workers_online, wproc_num_workers_spawned);

	if (wproc_num_workers_online != wproc_num_workers_spawned) {
		fail("%d/%d workers online",
		     wproc_num_workers_online, wproc_num_workers_spawned);
	}
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

	naemon_binary_path = argv[0];

	if (argc > 1) {
		if (!strcmp(argv[1], "--worker")) {
			return nm_core_worker(argv[2]);
		}
		return wrk_test_plugin(argc - 1, &argv[1]);
	}

	srunner_run_all(sr, CK_ENV);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return !failed ? EXIT_SUCCESS : EXIT_FAILURE;
}
