#include "naemon/events.h"
#include "naemon/query-handler.h"
#include "naemon/globals.h"
#include "naemon/workers.h"
#include "naemon/commands.h"
#include "naemon/logging.h"
#include "worker/worker.h"
#include "lib/libnaemon.h"
#include <check.h>
#include <string.h>

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
struct wrk_test {
	char *command;
	char *expected_stdout;
	char *expected_stderr;
	int expected_wait_status;
	int expected_error_code;
	int timeout;
};

static unsigned int completed_jobs;
void wrk_test_cb(struct wproc_result *wpres, void *data, int flags)
{
	struct wrk_test *t = (struct wrk_test *)data;
	completed_jobs++;

	ck_assert(wpres != NULL);
	ck_assert_msg(wpres->wait_status == t->expected_wait_status,
	   "wait_status: got %d, expected %d for command '%s'",
	   wpres->wait_status, t->expected_wait_status, t->command);
	ck_assert_msg(0 == strcmp(wpres->outstd, t->expected_stdout),
	   "STDOUT:\n###GOT\n%s\n##EXPECTED\n%s###STDOUT_END\ncommand: '%s'",
	   wpres->outstd, t->expected_stdout, t->command);

	ck_assert_int_eq(t->expected_error_code, wpres->error_code);
		ck_assert_msg(0 == strcmp(wpres->outerr, t->expected_stderr),
		"STDERR:\n###GOT\n%s###EXPECTED\n%s###STDERR_END\ncommand: '%s'",
		wpres->outerr, t->expected_stderr, t->command);

}

static void run_main_loop(time_t runtime) {
	time_t s, n;
	n = s = time(NULL);

	while (((runtime - (n - s)) > 0) && completed_jobs == 0) {
		iobroker_poll(nagios_iobs, 250);
		n = time(NULL);
	}
}

static void run_worker_test(struct wrk_test *j) {
	int ret = wproc_run_callback(j->command, j->timeout, wrk_test_cb, j, NULL);
	ck_assert_int_eq(0, ret);

	run_main_loop(j->timeout + 10);

	ck_assert_int_eq(1, completed_jobs);
}

static void test_debug_log_content(int should_exist, const char *expect) {
	char log_buffer[256*1024];
	size_t len;
	FILE *fp;
	int found;

	fp = fopen(debug_file, "r");
	len = fread(log_buffer, 1, sizeof(log_buffer)-1, fp);
	fclose(fp);
	log_buffer[len] = '\0';

	found = strstr(log_buffer, expect) == NULL ? FALSE : TRUE;

	if(should_exist) {
		ck_assert_msg(found, "Expected '%s' to be present in log", expect);
	} else {
		ck_assert_msg(!found, "Expected '%s' to not be present in log", expect);
	}
}

START_TEST(worker_test_output_stdout)
{
	struct wrk_test j = {
		"stdbuf -oL echo 'hello world'",
		"hello world\n",
		"",
		0, 0, 3
	};
	run_worker_test(&j);
}
END_TEST

START_TEST(worker_test_output_stderr)
{
	struct wrk_test j = {
		"stdbuf -e0 /bin/sh -c 'echo \"this goes to stderr\" >&2'",
		"",
		"this goes to stderr\n",
		0, 0, 3
	};
	run_worker_test(&j);
}
END_TEST

START_TEST(worker_test_output_staggered_output_and_exitcode_2)
{
	struct wrk_test j = {
		"/bin/sh -c 'echo -n natt; sleep 3; echo -n hatt; sleep 3; echo -n kattegatt; exit 2'",
		"natthattkattegatt",
		"",
		EXITCODE(2), 0, 7
	};
	run_worker_test(&j);
}
END_TEST

START_TEST(worker_test_output_mixed_stdout_and_stderr)
{
	struct wrk_test j = {
		"stdbuf -o0 -e0 /bin/sh -c 'echo -n nocrlf && echo -n lalala >&2'",
		"nocrlf",
		"lalala",
		0, 0, 3
	};
	run_worker_test(&j);
}
END_TEST

START_TEST(worker_test_timeout)
{
	struct wrk_test j = {
		"/bin/sh -c 'sleep 5'",
		"",
		"",
		0, ETIME, 3,
	};
	run_worker_test(&j);

	/*
	 * Verify that the log is correct.
	 * This is mostly to test the test environment for worker_test_no_timeout_log
	 */
	test_debug_log_content(TRUE, "due to timeout");
}
END_TEST

/*
 * There was a bug where an extra timeout job response were sent from the worker.
 * Verify that it doesn't happen.
 */
START_TEST(worker_test_no_timeout_log)
{
	struct wrk_test j = {
		"stdbuf -oL echo 'hello world'",
		"hello world\n",
		"",
		0, 0, 3
	};
	run_worker_test(&j);

	/* Wait a while longer, so timeout should trigger, even though worker test is executed above */
	completed_jobs = 0; /* To let mainloop run */
	run_main_loop(5);

	test_debug_log_content(FALSE, "due to timeout");
}
END_TEST

START_TEST(worker_test_output_stdout_and_timeout)
{
	struct wrk_test j = {
		"stdbuf -o0 /bin/sh -c 'echo -n lalala && sleep 5'",
		"lalala",
		"",
		0, ETIME, 3,
	};
	run_worker_test(&j);
}
END_TEST
/*
START_TEST(worker_test_child_remains_to_cause_sideeffects)
{
	char filepath[] = "/tmp/XXXXX-naemon-worker-test";
	int fd = mkstemp(filepath);
	int slept = 0;
	struct wrk_test j =	{
		NULL,
		"one\n",
		"",
		0, 0, 5,
	};
	close(fd);
	nm_asprintf(&j.command, "stdbuf -o0 /bin/sh -c '(echo one && (sleep 1 && echo two > %s&))'", filepath);
	run_worker_test(&j);
	nm_free(j.command);
	while (!g_file_test(filepath, G_FILE_TEST_EXISTS) && slept <= 3) {
		sleep(1);
		++slept;
	}
	ck_assert(g_file_test(filepath, G_FILE_TEST_EXISTS));
	unlink(filepath);
}
END_TEST
*/


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
	nagios_iobs = NULL;
}

void worker_test_setup(void)
{
	time_t start;
	int ret;
	completed_jobs = 0;

	/* Set temporary log file */
	log_file = strdup("/tmp/naemon-worker-test-log-XXXXXX");
	debug_file = strdup("/tmp/naemon-worker-test-log-XXXXXX");
	close(mkstemp(log_file));
	close(mkstemp(debug_file));
	logging_options = -1;
	debug_level = DEBUGL_ALL;
	debug_verbosity = DEBUGV_MORE;
	open_debug_log();

	init_iobroker();
	enable_timing_point = 1;
	qh_socket_path = "/tmp/qh-socket";
	qh_init(qh_socket_path);
	ck_assert_int_eq(0, wproc_num_workers_spawned);
	ret = init_workers(1);
	ck_assert_int_eq(0, ret);
	start = time(NULL);
	while (wproc_num_workers_online < wproc_num_workers_spawned && time(NULL) < start + 10) {
		iobroker_poll(nagios_iobs, 10);
	}
	ck_assert_int_eq(wproc_num_workers_spawned, wproc_num_workers_online);
}

void worker_test_teardown(void)
{
	free_worker_memory(WPROC_FORCE);
	deinit_iobroker();
	qh_deinit(qh_socket_path);

	/* We allocated a log file path earlier */
	close_debug_log();
	unlink(debug_file);
	free(debug_file);
	debug_file = NULL;
	close_log_file();
	unlink(log_file);
	free(log_file);
	log_file = NULL;
	logging_options = 0;

	wproc_num_workers_online = 0;
	wproc_num_workers_spawned = 0;
	wproc_num_workers_desired = 0;
}

Suite *worker_suite(void)
{
	Suite *s;
	TCase *tc_worker_output;
	TCase *tc_command_worker;

	s = suite_create("worker tests");

	tc_worker_output = tcase_create("worker reaping tests");
	tcase_add_checked_fixture(tc_worker_output, worker_test_setup, worker_test_teardown);
	tcase_add_test(tc_worker_output, worker_test_output_stdout);
	tcase_add_test(tc_worker_output, worker_test_output_stderr);
	tcase_add_test(tc_worker_output, worker_test_output_staggered_output_and_exitcode_2);
	tcase_add_test(tc_worker_output, worker_test_output_mixed_stdout_and_stderr);
	tcase_add_test(tc_worker_output, worker_test_timeout);
	tcase_add_test(tc_worker_output, worker_test_no_timeout_log);
	tcase_add_test(tc_worker_output, worker_test_output_stdout_and_timeout);
	//tcase_add_test(tc_worker_output, worker_test_child_remains_to_cause_sideeffects);
	suite_add_tcase(s, tc_worker_output);

	tc_command_worker = tcase_create("command worker tests");
	tcase_add_checked_fixture(tc_command_worker, init_iobroker, deinit_iobroker);
	tcase_add_test(tc_command_worker, command_worker_launch_shutdown_test);
	suite_add_tcase(s, tc_command_worker);

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
