#include <check.h>
#include <glib.h>
#include "naemon/objects_service.h"
#include "naemon/objects_command.h"
#include "naemon/objects_host.h"
#include "naemon/downtime.h"
#include "naemon/events.h"

#define TARGET_SERVICE_NAME "my_service"
#define TARGET_HOST_NAME "my_host"

const char *mocked_get_default_retention_file(void) {
	return "/tmp/naemon-test-scheduled-downtimes.retentiondata";
}

#define get_default_retention_file mocked_get_default_retention_file
#include "naemon/xrddefault.c"
#undef get_default_retention_file

static host *hst;
static service *svc;
static command *cmd;
void setup (void) {

	init_event_queue();
	init_objects_host(1);
	init_objects_service(1);
	init_objects_command(1);
	initialize_downtime_data();
	initialize_retention_data();

	cmd = create_command("my_command", "/bin/true");
	ck_assert(cmd != NULL);
	register_command(cmd);

	hst = create_host(TARGET_HOST_NAME);
	ck_assert(hst != NULL);
	hst->check_command_ptr = cmd;
	register_host(hst);

	svc = create_service(hst, TARGET_SERVICE_NAME);
	ck_assert(svc != NULL);
	svc->check_command_ptr = cmd;
	register_service(svc);

}

void teardown (void) {

	destroy_objects_command();
	destroy_objects_service();
	destroy_objects_host();
	cleanup_retention_data();
	cleanup_downtime_data();
	destroy_event_queue();
}

void simulate_naemon_reload(void) {
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
			"Some downtime comment", now, now+duration,
			fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);

	ck_assert_int_eq(0, hst->scheduled_downtime_depth);

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == TRUE);


	ck_assert(OK == unschedule_downtime(HOST_DOWNTIME, downtime_id));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == FALSE);
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
			"Some downtime comment", now, now+duration,
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
	ck_assert(dt->is_in_effect == FALSE);
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
			"Some downtime comment 1", now, now+duration,
			fixed, triggered_by, duration, &downtime_id);

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author 2",
			"Some downtime comment 2", now, now+duration,
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
	ck_assert(dt2->is_in_effect == FALSE);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id2));

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == FALSE);
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
			"Some downtime comment 1", now, now+duration,
			fixed, triggered_by, duration, &downtime_id);

	schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now, "Some downtime author 2",
			"Some downtime comment 2", now, now+duration,
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
	ck_assert(dt2->is_in_effect == FALSE);
	ck_assert(NULL == find_downtime(ANY_DOWNTIME, downtime_id2));

	ck_assert(OK == handle_scheduled_downtime(dt));
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->is_in_effect == FALSE);
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
			"Some downtime comment", now, now+duration,
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
			"Some downtime comment", now, now+duration,
			fixed, triggered_by, duration, &downtime_id);

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);
	ck_assert(0 == dt->comment_id);

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
			"Some downtime comment", now, now+duration,
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
			"Some downtime comment", now, now+duration,
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

	ck_assert(ERROR == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now-(duration*2), "Some downtime author",
			"Some downtime comment", now-(duration*2), now-duration,
			fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt == NULL);


	/* schedule a downtime that starts in the past, but ends in the future */
	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now - (duration*0.5), "Some downtime author",
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
	ck_assert(OK == schedule_downtime(HOST_DOWNTIME, TARGET_HOST_NAME, NULL, now - (duration*0.5), "Some downtime author",
				"Some downtime comment", now - (duration * 0.5), now + (duration * 0.5),
				fixed, triggered_by, duration, &downtime_id));

	dt = find_downtime(ANY_DOWNTIME, downtime_id);
	ck_assert(dt != NULL);
	ck_assert(dt->fixed == FALSE);
	ck_assert(dt->is_in_effect == FALSE);

	hst->current_state = STATE_DOWN;
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
	ck_assert(dt->start_event == NULL);
	ck_assert(OK == check_pending_flex_host_downtime(hst));
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
	ck_assert(dt->is_in_effect == FALSE);
	ck_assert(triggered_dt->is_in_effect == FALSE);
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
	ck_assert(dt->is_in_effect == FALSE);
	ck_assert(triggered_dt->is_in_effect == FALSE);
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
	ck_assert(dt->is_in_effect == FALSE);
	ck_assert(triggered_dt->is_in_effect == FALSE);
	ck_assert_int_eq(1, hst->scheduled_downtime_depth);

	/* ... but the regular downtime has to expire by itself (i.e, it's unaffected by the other ones) */
	ck_assert(OK == handle_scheduled_downtime(fixed_dt));
	ck_assert(fixed_dt->is_in_effect == FALSE);
	ck_assert_int_eq(0, hst->scheduled_downtime_depth);
}
END_TEST

Suite*
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
