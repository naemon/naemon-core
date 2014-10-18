#include <check.h>

/* yes, include C file, we should access static functions */
#include "naemon/events.c"

iobroker_set *nagios_iobs = NULL;

volatile sig_atomic_t sigshutdown = FALSE;
volatile sig_atomic_t sigrestart = FALSE;
volatile sig_atomic_t sigrotate = FALSE;

int log_debug_info(int tgt, int lvl, const char *fmt, ...) { return 0; }
void logit(int tgt, int lvl, const char *fmt, ...) {}
int rotate_log_file(time_t rotation_time) { return OK; }
int update_program_status(int aggregated_dump) { return OK; }
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
static void func_a(struct timed_event_properties *evprop) {
}

START_TEST(event_heap_count_ordered)
{
	struct timed_event_queue *q;
	struct timed_event *ev;
	size_t i;
	size_t count;
	size_t test_size = 17000;

	q = evheap_create(1);
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
	}

	count = 0;
	while((ev = evheap_head(q)) != NULL) {
		ck_assert_int_eq(count, (size_t)ev->event_time.tv_nsec);
		count++;
		evheap_remove(q, ev);
		free(ev);
	}
	ck_assert_int_eq(count, test_size);

	evheap_destroy(q);
}
END_TEST

START_TEST(event_heap_count_random_order)
{
	struct timed_event_queue *q;
	struct timed_event *ev;
	size_t i;
	size_t count;
	size_t test_size = 50000;

	q = evheap_create(1);
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
	}

	count = 0;
	i = 0;
	while((ev = evheap_head(q)) != NULL) {
		/* Make sure the value increments */
		ck_assert((size_t)ev->event_time.tv_sec >= i);
		i=(size_t)ev->event_time.tv_sec;
		count++;
		evheap_remove(q, ev);
		free(ev);
	}
	ck_assert_int_eq(count, test_size);

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
	suite_add_tcase(s, tc);

	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = event_heap_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
