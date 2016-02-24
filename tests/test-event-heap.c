#include <check.h>

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
static void func_a(struct nm_event_execution_properties *evprop) {
}

/* Verify the heap property: every node is less than its children */
static void verify_queue_heap(struct timed_event_queue *q) {
	size_t i;
	size_t child;

	_ck_assert_int(q->size, >=, q->count);

	for(i=0;i<q->count;i++) {
		child = i*2+1;
		if (child < q->count) {
			_ck_assert_int(evheap_compare(q->queue[i], q->queue[child]), <=, 0);
		}
		child = i*2+2;
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

	for(i=0; i<test_size; i++) {
		ev = nm_malloc(sizeof(struct timed_event));
		ev->callback = func_a;
		ev->event_time.tv_sec = i;
		ev->event_time.tv_nsec = i;
		ev->user_data = NULL;
		evheap_add(q, ev);

		/* Hard operation, don't do it every time, but often */
		if(i%500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);

	i = 0;
	for(i=0; i<test_size; i++) {
		ev = evheap_head(q);
		ck_assert(ev != NULL);
		ck_assert_int_eq(i, (size_t)ev->event_time.tv_nsec);
		evheap_remove(q, ev);
		free(ev);

		/* Hard operation, don't do it every time, but often */
		if(i%500 == 0)
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

	for(i=0; i<test_size; i++) {
		ev = nm_malloc(sizeof(struct timed_event));
		ev->callback = func_a;
		ev->event_time.tv_sec = rand();
		ev->event_time.tv_nsec = i;
		ev->user_data = NULL;
		evheap_add(q, ev);

		/* Hard operation, don't do it every time, but often */
		if(i%500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);

	ck_assert_int_eq(q->count, test_size);

	last_value = 0;
	for(i=0; i<test_size; i++) {
		ev = evheap_head(q);
		ck_assert(ev != NULL);

		/* Make sure the value increments */
		ck_assert((time_t)ev->event_time.tv_sec >= last_value);
		last_value=(time_t)ev->event_time.tv_sec;

		evheap_remove(q, ev);
		free(ev);

		/* Hard operation, don't do it every time, but often */
		if(i%500 == 0)
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

	for(i=0; i<test_size; i++) {
		ev = nm_malloc(sizeof(struct timed_event));
		ev->callback = func_a;
		ev->event_time.tv_sec = i;
		ev->event_time.tv_nsec = i;
		ev->user_data = NULL;
		evheap_add(q, ev);

		/* Hard operation, don't do it every time, but often */
		if(i%500 == 0)
			verify_queue_heap(q);
	}
	verify_queue_heap(q);

	ck_assert_int_eq(q->count, test_size);

	for(i=0; i<test_size; i++) {
		ck_assert_int_ne(q->count, 0);

		/* Pick an event at random */
		ev = q->queue[rand()%q->count];
		evheap_remove(q, ev);
		free(ev);

		/* Hard operation, don't do it every time, but often */
		if(i%500 == 0)
			verify_queue_heap(q);
	}
	ck_assert_int_eq(q->count, 0);
	ck_assert(evheap_head(q) == NULL);

	evheap_destroy(q);
}
END_TEST

Suite *event_heap_suite(void)
{
	Suite *s = suite_create("Event Heap");

	TCase *tc;
	tc = tcase_create("Event heap");
	tcase_add_test(tc, event_heap_count_ordered);
	tcase_add_test(tc, event_heap_count_random_order);
	tcase_add_test(tc, event_heap_count_random_removal);
	suite_add_tcase(s, tc);

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
