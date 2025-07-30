#include <check.h>
#include <glib.h>
#include "naemon/objects_service.h"
#include "naemon/objects_command.h"
#include "naemon/objects_host.h"
#include "naemon/downtime.h"
#include "naemon/events.h"
#include "naemon/checks.h"
#include "naemon/checks_service.h"
#include <sys/time.h>
#include <fcntl.h>

struct timed_event {
	size_t pos;
	struct timespec event_time;
	event_callback callback;
	void *user_data;
};

#define TARGET_SERVICE_NAME "my_service"
#define TARGET_SERVICE_NAME1 "my_service1"
#define TARGET_HOST_NAME "my_host"

#define STR_STRING_SERVICE_NOTIFICATION_ATTEMPT "** Service Notification Attempt **"
#define STR_STRING_HOST_NOTIFICATION_ATTEMPT "** Host Notification Attempt **"

const char *mocked_get_default_retention_file(void)
{
	return "/tmp/naemon-test-scheduled-downtimes.retentiondata";
}

#define get_default_retention_file mocked_get_default_retention_file
#include "naemon/xrddefault.c"
#undef get_default_retention_file

static host *hst;
static service *svc, *svc1;
static command *cmd;

void setup(void)
{

	int ret;
	char *workdir = NULL;
	init_event_queue();
	init_objects_host(1);
	init_objects_service(2);
	init_objects_command(1);
	initialize_downtime_data();
	initialize_comment_data();
	initialize_retention_data();
	workdir = getcwd(NULL, 0);

	ret = asprintf(&log_file, "%s/active.log", workdir);
	ck_assert(ret >= 0);

	debug_level = -1;
	debug_verbosity = 10;
	debug_file = log_file;


	/* Don't check return value, just make -Wno-unused-result happy */
	workdir = getcwd(NULL, 0);

	ret = asprintf(&log_file, "%s/active.log", workdir);
	ck_assert(ret >= 0);

	debug_level = -1;
	debug_verbosity = 10;
	debug_file = log_file;

	cmd = create_command("my_command", "/bin/true");
	ck_assert(cmd != NULL);
	register_command(cmd);

	hst = create_host(TARGET_HOST_NAME);
	ck_assert(hst != NULL);
	hst->check_command_ptr = cmd;
	hst->check_command = nm_strdup("something or other");;
	register_host(hst);

	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	register_service(svc);

	svc1 = create_service(hst, TARGET_SERVICE_NAME1);
	ck_assert(svc1 != NULL);
	svc1->check_command_ptr = cmd;
	register_service(svc1);

}

void teardown(void)
{

	destroy_objects_command();
	destroy_objects_service(TRUE);
	destroy_objects_host();
	cleanup_retention_data();
	cleanup_downtime_data();
	destroy_event_queue();
	free(log_file);
}

void simulate_naemon_reload(void)
{
	/* dirty hacks to simulate a "reload"*/
	retain_state_information = TRUE;
	temp_file = "/tmp/naemon-test-scheduled-downtimes.retentiondata.tmp";
	ck_assert(OK == save_state_information(
	              0 /* this parameter does nothing ... */
	          ));
	teardown(); /* uh-oh! */
	setup();

	ck_assert(OK == read_initial_state_information());
}

START_TEST(host_fixed_scheduled_downtime_cancelled)
{
	time_t now = time(NULL);
	int fixed = 1;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                  "Some downtime comment", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == TRUE);


	ck_assert(OK == unschedule_downtime(HOST_DOWNTIME, downtime_id));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id));
}
END_TEST

START_TEST(host_fixed_scheduled_downtime_stopped)
{
	time_t now = time(NULL);
	int fixed = 1;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                  "Some downtime comment", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == TRUE);


	/*
	 * It's okay to stop a downtime this way even though it has not yet
	 * expired since the start/stop times are only used by the event scheduler
	 * to determine when start/stop events should be fired
	 */
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id));
}
END_TEST

START_TEST(host_multiple_fixed_scheduled_downtimes)
{
	time_t now = time(NULL);
	int fixed = 1;
	unsigned long downtime_id;
	unsigned long downtime_id2;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL, *dt2 = NULL;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author 1",
	                  "Some downtime comment 1", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author 2",
	                  "Some downtime comment 2", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id2);


	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	dt2 = find_downtime(ANY_DOWNTIME, downtime_id2);
	ck_assert(dt2 != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == TRUE);

	ck_assert(OK == handle_scheduled_downtime(dt2));
	ck_assert_int_eq(2, hst->scheduled_downtime_depth);
	ck_assert(dt2->is_in_effect == TRUE);

	ck_assert(OK == handle_scheduled_downtime(dt2));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id2));

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id));
}
END_TEST

START_TEST(host_multiple_fixed_scheduled_downtimes_one_cancelled_one_stopped)
{
	time_t now = time(NULL);
	int fixed = 1;
	unsigned long downtime_id;
	unsigned long downtime_id2;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL, *dt2 = NULL;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author 1",
	                  "Some downtime comment 1", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author 2",
	                  "Some downtime comment 2", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id2);


	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	dt2 = find_downtime(ANY_DOWNTIME, downtime_id2);
	ck_assert(dt2 != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == TRUE);

	ck_assert(OK == handle_scheduled_downtime(dt2));
	ck_assert_int_eq(2, hst->scheduled_downtime_depth);
	ck_assert(dt2->is_in_effect == TRUE);

	ck_assert(OK == unschedule_downtime(HOST_DOWNTIME, downtime_id2));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id2));

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id));
}
END_TEST

START_TEST(host_fixed_scheduled_downtime_depth_retained_across_reload)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                  "Some downtime comment", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == FALSE);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);

	simulate_naemon_reload();
	dt = find_downtime(ANY_DOWNTIME, downtime_id);

	ck_assert(dt->is_in_effect == TRUE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id));
}
END_TEST

START_TEST(host_downtime_id_retained_across_reload)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;
	unsigned long comment_id;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                  "Some downtime comment", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);
	ck_assert(1 == dt->comment_id);

	ck_assert(OK == handle_scheduled_downtime(dt));
	comment_id = dt->comment_id;

	simulate_naemon_reload();
	dt = find_downtime(ANY_DOWNTIME, downtime_id);

	ck_assert_int_eq(comment_id, dt->comment_id);
}
END_TEST

START_TEST(host_flexible_scheduled_downtime)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                  "Some downtime comment", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == FALSE);

	/**
	 * NOTE: In reality, this "event" would be triggered by the fact
	 * that the host went down. Since we don't want to muddy up these
	 * tests with the entire check processing chain, we simply pretend that
	 * that just happened and call handle_scheduled_downtime ourselves.
	 */
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id));
}
END_TEST

START_TEST(host_flexible_scheduled_downtime_across_reload)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                  "Some downtime comment", now, now + duration,
	                  fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == FALSE);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);

	simulate_naemon_reload();
	dt = find_downtime(ANY_DOWNTIME, downtime_id);

	ck_assert(dt->is_in_effect == TRUE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id));
}
END_TEST

START_TEST(host_flexible_scheduled_downtime_in_the_past)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;

	ck_assert(ERROR == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now - (duration * 2), "Some downtime author",
	                                     "Some downtime comment", now - (duration * 2), now - duration,
	                                     fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt == NULL);


	/* schedule a downtime that starts in the past, but ends in the future */
	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now - (duration * 0.5), "Some downtime author",
	                                  "Some downtime comment", now - (duration * 0.5), now + (duration * 0.5),
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);
	ck_assert(dt->fixed == FALSE);
}
END_TEST

START_TEST(host_flexible_scheduled_downtime_triggered_when_host_down)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;

	/* schedule a downtime that starts in the past, but ends in the future */
	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now - (duration * 0.5), "Some downtime author",
	                                  "Some downtime comment", now - (duration * 0.5), now + (duration * 0.5),
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);
	ck_assert(dt->fixed == FALSE);
	ck_assert(dt->is_in_effect == FALSE);

	hst->current_state = STATE_DOWN;
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->start_event == NULL);
	ck_assert(OK <= check_pending_flex_host_downtime(hst));
	ck_assert(dt->start_event != NULL);
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
}
END_TEST


START_TEST(host_triggered_scheduled_downtime)
{
	time_t now = time(NULL);
	int fixed = 1;
	unsigned long downtime_id;
	unsigned long triggered_downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;
	scheduled_downtime *triggered_dt = NULL;

	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	/* now schedule a downtime that's triggered by the one we just scheduled */
	triggered_by = downtime_id;
	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &triggered_downtime_id));

	triggered_dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(triggered_dt != NULL);

	/* the triggered downtime should be triggered by the first downtime ...*/
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert(triggered_dt->is_in_effect == TRUE);
	ck_assert_int_eq(2, hst->scheduled_downtime_depth);


	/* ... and the triggered downtime should expire when the first downtime does */
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
}
END_TEST

START_TEST(host_triggered_scheduled_downtime_across_reload)
{
	time_t now = time(NULL);
	int fixed = 1;
	unsigned long downtime_id;
	unsigned long triggered_downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;
	scheduled_downtime *triggered_dt = NULL;

	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	/* now schedule a downtime that's triggered by the one we just scheduled */
	triggered_by = downtime_id;
	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &triggered_downtime_id));

	triggered_dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(triggered_dt != NULL);

	/* the triggered downtime should be triggered by the first downtime ...*/
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert(triggered_dt->is_in_effect == TRUE);
	ck_assert_int_eq(2, hst->scheduled_downtime_depth);

	simulate_naemon_reload();
	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	triggered_dt = find_downtime(ANY_DOWNTIME, triggered_downtime_id);

	ck_assert(dt->is_in_effect == TRUE);
	ck_assert(triggered_dt->is_in_effect == TRUE);
	ck_assert_int_eq(2, hst->scheduled_downtime_depth);

	/* ... and the triggered downtime should expire when the first downtime does */
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
}
END_TEST

START_TEST(host_triggered_and_fixed_scheduled_downtime)
{
	time_t now = time(NULL);
	int fixed = 1;
	unsigned long downtime_id;
	unsigned long triggered_downtime_id;
	unsigned long fixed_downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;
	scheduled_downtime *triggered_dt = NULL;
	scheduled_downtime *fixed_dt = NULL;

	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &fixed_downtime_id));


	fixed_dt = find_downtime(ANY_DOWNTIME, fixed_downtime_id);
	ck_assert(dt != NULL);

	/* now schedule a downtime that's triggered by the one we just scheduled */
	triggered_by = downtime_id;
	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &triggered_downtime_id));

	triggered_dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(triggered_dt != NULL);

	/* schedule a regular downtime that doesn't trigger anything*/
	ck_assert(OK == handle_scheduled_downtime(fixed_dt));
	ck_assert(fixed_dt->is_in_effect == TRUE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);


	/* the triggered downtime should be triggered by the first downtime ...*/
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert(triggered_dt->is_in_effect == TRUE);
	ck_assert_int_eq(3, hst->scheduled_downtime_depth);

	/* ... and the triggered downtime should expire when the first downtime does ... */
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);

	/* ... but the regular downtime has to expire by itself (i.e, it's unaffected by the other ones) */
	ck_assert(OK == handle_scheduled_downtime(fixed_dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
}
END_TEST

START_TEST(service_triggered_scheduled_downtime)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long triggered_downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	scheduled_downtime *dt = NULL;
	scheduled_downtime *triggered_dt = NULL;
	struct timespec event_time_stop_triggered;
	struct check_result cr ;

	cr.object_check_type = SERVICE_CHECK;
	cr.host_name = TARGET_HOST_NAME;
	cr.service_description = TARGET_SERVICE_NAME;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.check_options = 0;
	cr.scheduled_check = TRUE;
	cr.latency = 10;
	cr.early_timeout = FALSE;
	cr.exited_ok = TRUE;
	cr.return_code = STATE_DOWN;
	cr.output = "CHECK_NRPE: Socket timeout after 10 seconds.";
	cr.source = NULL;
	cr.engine = NULL;

	clock_gettime(CLOCK_MONOTONIC, &event_time_stop_triggered);

	ck_assert(OK == schedule_downtime(SERVICE_DOWNTIME, TARGET_HOST_NAME, TARGET_SERVICE_NAME, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(SERVICE_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	/* now schedule a downtime that's triggered by the one we just scheduled */
	triggered_by = downtime_id;
	ck_assert(OK == schedule_downtime(SERVICE_DOWNTIME, TARGET_HOST_NAME, TARGET_SERVICE_NAME1, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &triggered_downtime_id));

	triggered_dt = find_downtime(ANY_DOWNTIME, triggered_downtime_id);
	ck_assert(triggered_dt != NULL);

	/* the triggered downtime should be triggered by the first downtime ...*/
	process_check_result(&cr);
	//Would be nice to just use the function call like destroy_event(dt->start_event) but the event handler is not set up in this test environment

	dt->flex_downtime_start = now;
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert(dt->is_in_effect == TRUE);
	ck_assert(triggered_dt->is_in_effect == TRUE);
	/* just to make sure that the triggered flex_downtime_start is the same as the triggering downtime's flex_downtime_start	 */
	ck_assert(triggered_dt->flex_downtime_start == dt->flex_downtime_start);
	/* make sure the stop event is scheduled after the current time */
	ck_assert(event_time_stop_triggered.tv_sec < triggered_dt->stop_event->event_time.tv_sec);
	ck_assert_int_eq(1, svc->scheduled_downtime_depth);
	ck_assert_int_eq(1, svc1->scheduled_downtime_depth);


	/* ... and the triggered downtime should expire when the first downtime does */
	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, svc->scheduled_downtime_depth);
	ck_assert_int_eq(0, svc1->scheduled_downtime_depth);
}
END_TEST


/* The test case is added for the notification of the service flexible downtime.
 * The test will pass a failure service check_result to the process_check_result.
 * In our case, the notification should not be sent as the service is entering downtime due to this check failure
 */
START_TEST(service_flexible_scheduled_downtimes_service_down_notification)
{
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	int fd;
	scheduled_downtime *dt = NULL;
	struct check_result cr ;
	char active_contents[1024];
	size_t len;

	/* fill the check_result struct for the service check failure */
	cr.object_check_type = SERVICE_CHECK;
	cr.host_name = TARGET_HOST_NAME;
	cr.service_description = TARGET_SERVICE_NAME;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.check_options = 0;
	cr.scheduled_check = TRUE;
	cr.latency = 10;
	cr.early_timeout = FALSE;
	cr.exited_ok = TRUE;
	cr.return_code = STATE_DOWN;
	cr.output = "CHECK_NRPE: Socket timeout after 10 seconds.";
	cr.source = NULL;
	cr.engine = NULL;

	/* Make sure failure check will set the service to down state */
	svc->max_attempts = 0;

	ck_assert(OK == schedule_downtime(SERVICE_DOWNTIME, TARGET_HOST_NAME, TARGET_SERVICE_NAME, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);

	ck_assert(dt != NULL);

	ck_assert_int_eq(0, gettimeofday(&(cr.start_time), NULL));
	ck_assert_int_eq(0, gettimeofday(&(cr.finish_time), NULL));

	/*Use a temporary file to catch if notification happens, simply because there is not other way (any global variables)
	* to check whether it is trying to call the notification function
	*/
	open_debug_log();

	// This will create a handle_downtime_start_event on the event queue
	process_check_result(&cr);

	close_debug_log();

	fd = open(log_file, O_RDONLY);
	len = read(fd, active_contents, 1024);
	active_contents[len] = '\0';
	close(fd);
	unlink(log_file);

	// Make sure the event is created for this downtime start on the event queue
	ck_assert(dt->start_event != NULL);
	ck_assert(strstr(active_contents, STR_STRING_SERVICE_NOTIFICATION_ATTEMPT) == NULL);

	// Clean up the downtime start event created on the event queue
	destroy_event(dt->start_event);
	ck_assert(OK == delete_service_downtime(downtime_id));
}
END_TEST

/* The test case is added for the notification of the host flexible downtime.
 * The test will pass a failure host check_result to the process_check_result.
 * In our case, the notification should not be sent as the host is entering downtime due to this check failure
 */
START_TEST(host_flexible_scheduled_downtimes_service_down_notification)
{
	/* The host flexible scheduled downtime apparently works a little different than the service flexible scheduled downtime
	 * The first event down will trigger the downtime start on the host while maximum retried has been reached then the downtime will be started.
	 */
	time_t now = time(NULL);
	int fixed = 0;
	unsigned long downtime_id;
	unsigned long duration = 60;
	unsigned long triggered_by = 0;
	int fd;
	scheduled_downtime *dt = NULL;
	struct check_result cr ;
	char active_contents[1024];
	size_t len;

	cr.object_check_type = HOST_CHECK;
	cr.host_name = TARGET_HOST_NAME;
	cr.service_description = NULL;
	cr.check_type = CHECK_TYPE_ACTIVE;
	cr.check_options = 0;
	cr.scheduled_check = TRUE;
	cr.latency = 10;
	cr.early_timeout = FALSE;
	cr.exited_ok = TRUE;
	cr.return_code = STATE_CRITICAL;
	cr.output = "is DOWN - rta: nan, lost 100%|pkt=5;5;5;5;5 rta=0.0;2000.000;2000.000;; pl=100%;95;100;;";
	cr.source = NULL;
	cr.engine = NULL;

	hst->current_state = STATE_UP;

	hst->max_attempts = 1;

	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author",
	                                  "Some downtime comment", now, now + duration,
	                                  fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);

	ck_assert(dt != NULL);

	ck_assert_int_eq(0, gettimeofday(&(cr.start_time), NULL));
	ck_assert_int_eq(0, gettimeofday(&(cr.finish_time), NULL));

	/* Use a temporary file to catch if notification happens, simply because there is not other way (any global variables)
	 * to check whether it is trying to call the notification function
	 */
	open_debug_log();

	// This will create a handle_downtime_start_event on the event queue
	ck_assert(OK == process_check_result(&cr));

	close_debug_log();

	fd = open(log_file, O_RDONLY);
	len = read(fd, active_contents, 1024);
	active_contents[len] = '\0';
	close(fd);
	unlink(log_file);

	// Make sure the event is created for this downtime start on the event queue
	ck_assert(dt->start_event != NULL);
	ck_assert(strstr(active_contents, STR_STRING_HOST_NOTIFICATION_ATTEMPT) == NULL);

	// Clean up the downtime start event created on the event queue
	destroy_event(dt->start_event);
	ck_assert(OK == delete_host_downtime(downtime_id));
}
END_TEST

Suite *
scheduled_downtimes_suite(void)
{
	Suite *s = suite_create("Scheduled downtimes");

	TCase *tc_fixed_scheduled_downtimes = tcase_create("Fixed scheduled downtimes");
	TCase *tc_flexible_scheduled_downtimes = tcase_create("Flexible scheduled downtimes");
	TCase *tc_triggered_scheduled_downtimes = tcase_create("Triggered scheduled downtimes");
	tcase_add_checked_fixture(tc_fixed_scheduled_downtimes, setup, teardown);
	tcase_add_checked_fixture(tc_flexible_scheduled_downtimes, setup, teardown);
	tcase_add_checked_fixture(tc_triggered_scheduled_downtimes, setup, teardown);

	tcase_add_test(tc_fixed_scheduled_downtimes, host_fixed_scheduled_downtime_cancelled);
	tcase_add_test(tc_fixed_scheduled_downtimes, host_fixed_scheduled_downtime_stopped);
	tcase_add_test(tc_fixed_scheduled_downtimes, host_fixed_scheduled_downtime_depth_retained_across_reload);
	tcase_add_test(tc_fixed_scheduled_downtimes, host_downtime_id_retained_across_reload);
	tcase_add_test(tc_fixed_scheduled_downtimes, host_multiple_fixed_scheduled_downtimes);
	tcase_add_test(tc_fixed_scheduled_downtimes, host_multiple_fixed_scheduled_downtimes_one_cancelled_one_stopped);

	tcase_add_test(tc_flexible_scheduled_downtimes, host_flexible_scheduled_downtime);
	tcase_add_test(tc_flexible_scheduled_downtimes, host_flexible_scheduled_downtime_across_reload);
	tcase_add_test(tc_flexible_scheduled_downtimes, host_flexible_scheduled_downtime_in_the_past);
	tcase_add_test(tc_flexible_scheduled_downtimes, host_flexible_scheduled_downtime_triggered_when_host_down);
	tcase_add_test(tc_flexible_scheduled_downtimes, service_flexible_scheduled_downtimes_service_down_notification);
	tcase_add_test(tc_flexible_scheduled_downtimes, host_flexible_scheduled_downtimes_service_down_notification);

	tcase_add_test(tc_triggered_scheduled_downtimes, service_triggered_scheduled_downtime);
	tcase_add_test(tc_triggered_scheduled_downtimes, host_triggered_scheduled_downtime);
	tcase_add_test(tc_triggered_scheduled_downtimes, host_triggered_scheduled_downtime_across_reload);
	tcase_add_test(tc_triggered_scheduled_downtimes, host_triggered_and_fixed_scheduled_downtime);

	suite_add_tcase(s, tc_triggered_scheduled_downtimes);
	suite_add_tcase(s, tc_flexible_scheduled_downtimes);
	suite_add_tcase(s, tc_fixed_scheduled_downtimes);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = scheduled_downtimes_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
