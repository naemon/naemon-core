#include <check.h>
#include <stdio.h>
/* yes, include C file, we should access static functions */
#include "naemon/events.c"

/*
static void print_heap(struct timed_event_queue *q, size_t i) {
	size_t depth;
	struct timed_event *ev;
	if(i >= q->count)
		return;
	if(i==0) printf("\n");
	ev = q->queue[i];
	printf("%3lu %3lu ", i, ev->pos);
	for(depth = i; depth>0; depth = ((depth-1)>>1))
		printf("  ");
	printf("%lu %lu\n", ev->event_time.tv_sec, ev->event_time.tv_nsec);
	print_heap(q, (i<<1) + 1);
	print_heap(q, (i<<1) + 2);
}
*/
static void func_a(struct nm_event_execution_properties *evprop)
{
}

/* Verify the heap property: every node is less than its children */
static void verify_queue_heap(struct timed_event_queue *q)
{
	size_t i;
	size_t child;

	_ck_assert_int(q->size, >=, q->count);

	for (i = 0; i < q->count; i++) {
		child = i * 2 + 1;
		if (child < q->count) {
			_ck_assert_int(evheap_compare(q->queue[i], q->queue[child]), <=, 0);
		}
		child = i * 2 + 2;
		if (child < q->count) {
			_ck_assert_int(evheap_compare(q->queue[i], q->queue[child]), <=, 0);
		}
	}

}

START_TEST(event_heap_count_ordered)
{
	struct timed_event_queue *q;
	struct timed_event *ev;
	size_t i;
	size_t test_size = 10000;

	q = evheap_create();
	ck_assert_int_eq(q->count, 0);
	ck_assert_int_eq(q->size, 1);
	ck_assert(q->queue != NULL);

	for (i = 0; i < test_size; i++) {
		ev = nm_malloc(sizeof(struct timed_event));
		ev->callback = func_a;
		ev->event_time.tv_sec = i;
		ev->event_time.tv_nsec = i;
		ev->user_data = NULL;
		evheap_add(q, ev);

		/* Hard operation, don't do it every time, but often */
		if (i % 500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);

	i = 0;
	for (i = 0; i < test_size; i++) {
		ev = evheap_head(q);
		ck_assert(ev != NULL);
		ck_assert_int_eq(i, (size_t)ev->event_time.tv_nsec);
		evheap_remove(q, ev);
		free(ev);

		/* Hard operation, don't do it every time, but often */
		if (i % 500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);
	ck_assert_int_eq(q->count, 0);

	evheap_destroy(q);
}
END_TEST

START_TEST(event_heap_count_random_order)
{
	struct timed_event_queue *q;
	struct timed_event *ev;
	size_t i;
	size_t test_size = 10000;
	time_t last_value;

	q = evheap_create();
	ck_assert_int_eq(q->count, 0);
	ck_assert_int_eq(q->size, 1);
	ck_assert(q->queue != NULL);

	for (i = 0; i < test_size; i++) {
		ev = nm_malloc(sizeof(struct timed_event));
		ev->callback = func_a;
		ev->event_time.tv_sec = rand();
		ev->event_time.tv_nsec = i;
		ev->user_data = NULL;
		evheap_add(q, ev);

		/* Hard operation, don't do it every time, but often */
		if (i % 500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);

	ck_assert_int_eq(q->count, test_size);

	last_value = 0;
	for (i = 0; i < test_size; i++) {
		ev = evheap_head(q);
		ck_assert(ev != NULL);

		/* Make sure the value increments */
		ck_assert((time_t)ev->event_time.tv_sec >= last_value);
		last_value = (time_t)ev->event_time.tv_sec;

		evheap_remove(q, ev);
		free(ev);

		/* Hard operation, don't do it every time, but often */
		if (i % 500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);

	/*
	 * last_value _can_ be 0, but it's one in RAND_MAX^test_size that it is.
	 * This assertion verifies that the value actually has increased, nad tv_sec
	 * is set properly
	 */
	ck_assert_int_ne(last_value, 0);

	ck_assert_int_eq(q->count, 0);

	evheap_destroy(q);
}
END_TEST

START_TEST(event_heap_count_random_removal)
{
	struct timed_event_queue *q;
	struct timed_event *ev;
	size_t i;
	size_t test_size = 10000;

	q = evheap_create();
	ck_assert_int_eq(q->count, 0);
	ck_assert_int_eq(q->size, 1);
	ck_assert(q->queue != NULL);

	for (i = 0; i < test_size; i++) {
		ev = nm_malloc(sizeof(struct timed_event));
		ev->callback = func_a;
		ev->event_time.tv_sec = i;
		ev->event_time.tv_nsec = i;
		ev->user_data = NULL;
		evheap_add(q, ev);

		/* Hard operation, don't do it every time, but often */
		if (i % 500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);

	ck_assert_int_eq(q->count, test_size);

	for (i = 0; i < test_size; i++) {
		ck_assert_int_ne(q->count, 0);

		/* Pick an event at random */
		ev = q->queue[rand() % q->count];
		evheap_remove(q, ev);
		free(ev);

		/* Hard operation, don't do it every time, but often */
		if (i % 500 == 0)
			verify_queue_heap(q);
	}
	ck_assert_int_eq(q->count, 0);
	ck_assert(evheap_head(q) == NULL);

	evheap_destroy(q);
}
END_TEST

static struct nm_event_execution_properties *cb_props_param;
static iobroker_set *iobs;
void test_event_callback(struct nm_event_execution_properties *props)
{
	cb_props_param = nm_calloc(1, sizeof(*cb_props_param));
	memcpy(cb_props_param, props, sizeof(*props));
}

void event_polling_setup(void)
{
	init_event_queue();
	iobs = iobroker_create();
	ck_assert(iobs != NULL);
}

void event_polling_teardown(void)
{
	iobroker_destroy(iobs, 0);
	destroy_event_queue();
	nm_free(cb_props_param);
}

static time_t runnable_delays[] = {
	-14, /*a few seconds in the past*/
	    0, /*right now*/
	    -1462143350, /*a couple of years in the past*/
	    -9999999999, /*a few hundred years in the past */
	    -1, /* one second ago */
	    EVENT_MAX_POLL_TIME_MS / 1000,
	    LONG_MIN / 10,
	    -(1LL << 62)
    };

static int64_t unrunnable_delays[] = {
	1, /*a second too late*/
	14,
	1462143350,
	9999999999
};

START_TEST(event_polling_scheduling_past)
{
	int *user_data = malloc(sizeof(int));
	*user_data = _i;
	ck_assert(schedule_event(runnable_delays[_i], test_event_callback, user_data) != NULL);
	ck_assert_int_eq(0, event_poll_full(iobs, EVENT_MAX_POLL_TIME_MS));
	ck_assert_msg(cb_props_param != NULL, "Event scheduled with delay %llu was never executed", (long long int)runnable_delays[_i]);
	ck_assert_int_eq(*(int *)user_data, *(int *)(cb_props_param->user_data));
	free(user_data);
}
END_TEST

START_TEST(event_polling_scheduling_future)
{
	time_t delay = (EVENT_MAX_POLL_TIME_MS / 1000) + unrunnable_delays[_i];
	ck_assert(schedule_event(delay, test_event_callback, NULL) != NULL);
	ck_assert_int_eq(0, event_poll_full(iobs, 10));
	ck_assert_msg(cb_props_param == NULL, "Event scheduled with delay %llu was executed even though it's in the future", (long long int)delay);
}
END_TEST

START_TEST(event_timespec_msdiff)
{
	int64_t diff_s = 0, expected = 0;
	struct timespec ts1, ts2;
	ts1.tv_nsec = 0;
	ts1.tv_sec = 0;

	ts2.tv_nsec = 0;
	ts2.tv_sec = 0;

	diff_s = timespec_msdiff(&ts1, &ts2) / 1000;
	ck_assert_int_eq(expected, diff_s);

	ts1.tv_sec = expected = -(1LL << 2);
	diff_s = timespec_msdiff(&ts1, &ts2) / 1000;
	ck_assert_int_eq(expected, diff_s);

	ts1.tv_sec = expected = -(1LL << 31);
	diff_s = timespec_msdiff(&ts1, &ts2) / 1000;
	ck_assert_int_eq(expected, diff_s);

	ts1.tv_sec = LONG_MAX / 10;
	diff_s = timespec_msdiff(&ts1, &ts2) / 1000;
	ck_assert(diff_s > 0);

	ts1.tv_sec = LONG_MIN / 10;
	diff_s = timespec_msdiff(&ts1, &ts2) / 1000;
	ck_assert(diff_s < 0);
}
END_TEST

Suite *event_heap_suite(void)
{
	Suite *s = suite_create("Events");
	TCase *tc_event_heap, *tc_event_polling;

	tc_event_heap = tcase_create("Event heap");
	tcase_add_test(tc_event_heap, event_heap_count_ordered);
	tcase_add_test(tc_event_heap, event_heap_count_random_order);
	tcase_add_test(tc_event_heap, event_heap_count_random_removal);
	tcase_add_test(tc_event_heap, event_timespec_msdiff);
	suite_add_tcase(s, tc_event_heap);

	tc_event_polling = tcase_create("Event polling");
	tcase_add_loop_test(tc_event_polling, event_polling_scheduling_past, 0, ARRAY_SIZE(runnable_delays));
	tcase_add_loop_test(tc_event_polling, event_polling_scheduling_future, 0, ARRAY_SIZE(unrunnable_delays));
	tcase_add_checked_fixture(tc_event_polling, event_polling_setup, event_polling_teardown);
	suite_add_tcase(s, tc_event_polling);

	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = event_heap_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
