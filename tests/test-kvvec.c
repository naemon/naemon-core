#include <check.h>
#include <lib/kvvec.h>
#include <stdio.h>


static int walking_steps, walks;



static int kv_compare(const void *a_, const void *b_)
{
	const struct key_value *a = (const struct key_value *)a_;
	const struct key_value *b = (const struct key_value *)b_;
	int ret = 0;

	ret = strcmp(a->key, b->key);
	if (ret)
		return ret;

	if (!a->value && !b->value) {
		return 0;
	}
	if (a->value && !b->value)
		return -1;
	if (!a->value && b->value)
		return 1;

	return strcmp(a->value, b->value);
}

static int walker(struct key_value *kv, void *discard)
{
	static struct kvvec *vec = (void *)1;
	static int step;

	walking_steps++;

	if (vec != discard) {
		walks++;
		vec = (struct kvvec *)discard;
		step = 0;
	}

	if (discard && vec) {
		ck_assert_msg(!kv_compare(&vec->kv[step], kv), "step %d on walk %d",
		              step, walks);
	}

	step++;

	return 0;
}

#define KVSEP '='
#define PAIRSEP '\0'
#define OVERALLOC 2

static const char *test_data[] = {
	"lala=trudeldudel",
	"foo=bar",
	"LOTS AND LOTS OF CAPS WITH SPACES=weird",
	"key=value",
	"something-random=pre-determined luls",
	"string=with\nnewlines\n\n\nand\nlots\nof\nthem\ntoo\n",
	"tabs=	this	and		that			and three in a row",
	NULL,
};

static const char *pair_term_missing[] = {
	"foo=bar;lul=bar;haha=lulu",
	"foo=bar;lul=bar;haha=lulu;",
	"hobbit=palace;gandalf=wizard1",
	"hobbit=palace;gandalf=wizard1;",
	"0=0;1=1;2=2;3=3;4=4",
	"0=0;1=1;2=2;3=3;4=4;",
	NULL,
};

static void add_vars(struct kvvec *kvv, const char **ary, int len)
{
	int i;

	for (i = 0; i < len && ary[i]; i++) {
		char *arg = strdup(test_data[i]);
		char *eq = strchr(arg, '=');
		if (eq) {
			*eq++ = 0;
		}
		kvvec_addkv_str(kvv, strdup(arg), eq ? strdup(eq) : NULL);
		free(arg);
	}
}

/*
 * This tests to unpack then pack, and see if everything is back again.
 *
 * Because the previous test verifies that the data is unpacked correctly,
 * using that information, the data must here also be unpacked
 */
START_TEST(kvvec_tests)
{
	int i, j;
	struct kvvec *kvv, *kvv2, *kvv3;
	struct kvvec_buf *kvvb, *kvvb2;
	struct kvvec k = KVVEC_INITIALIZER;

	kvv = kvvec_create(1);
	ck_assert_int_eq(kvvec_capacity(kvv), 1);
	kvv2 = kvvec_create(1);
	kvv3 = kvvec_create(1);
	add_vars(kvv, test_data, 1239819);

	kvvec_sort(kvv);
	kvvec_foreach(kvv, NULL, walker);

	/* kvvec2buf -> buf2kvvec -> kvvec2buf -> buf2kvvec conversion */
	kvvb = kvvec2buf(kvv, KVSEP, PAIRSEP, OVERALLOC);
	kvv3 = buf2kvvec(kvvb->buf, kvvb->buflen, KVSEP, PAIRSEP, KVVEC_COPY);
	kvvb2 = kvvec2buf(kvv3, KVSEP, PAIRSEP, OVERALLOC);

	buf2kvvec_prealloc(kvv2, kvvb->buf, kvvb->buflen, KVSEP, PAIRSEP, KVVEC_ASSIGN);
	kvvec_foreach(kvv2, kvv, walker);

	kvvb = kvvec2buf(kvv, KVSEP, PAIRSEP, OVERALLOC);

	ck_assert_msg(kvv->kv_pairs == kvv2->kv_pairs, "pairs should be identical");

	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv1, *kv2;
		kv1 = &kvv->kv[i];
		if (i >= kvv2->kv_pairs) {
			printf("[%s=%s] (%d+%d)\n", kv1->key, kv1->value, kv1->key_len, kv1->value_len);
			ck_abort_msg("missing var %d in kvv2", i);
			continue;
		}
		kv2 = &kvv2->kv[i];
		ck_assert_msg(!kv_compare(kv1, kv2), "kv pair %d must match ([%s=%s] (%d+%d) != [%s=%s (%d+%d)])", i,
		              kv1->key, kv1->value, kv1->key_len, kv1->value_len,
		              kv2->key, kv2->value, kv2->key_len, kv2->value_len
		             );
	}

	ck_assert_msg(kvvb2->buflen == kvvb->buflen, "buflens must match");
	ck_assert_msg(kvvb2->bufsize == kvvb->bufsize, "bufsizes must match");

	ck_assert(kvvb2->buflen == kvvb->buflen);
	ck_assert(kvvb2->bufsize == kvvb->bufsize);
	ck_assert(!memcmp(kvvb2->buf, kvvb->buf, kvvb->bufsize));

	free(kvvb->buf);
	free(kvvb);
	free(kvvb2->buf);
	free(kvvb2);
	kvvec_destroy(kvv, 1);
	kvvec_destroy(kvv3, KVVEC_FREE_ALL);

	for (j = 0; pair_term_missing[j]; j++) {
		buf2kvvec_prealloc(&k, strdup(pair_term_missing[j]), strlen(pair_term_missing[j]), '=', ';', KVVEC_COPY);
		for (i = 0; i < k.kv_pairs; i++) {
			struct key_value *kv = &k.kv[i];
			ck_assert_msg(kv->key_len == kv->value_len, "%d.%d; key_len=%d; value_len=%d (%s = %s)",
			              j, i, kv->key_len, kv->value_len, kv->key, kv->value);
			ck_assert_msg(kv->value_len == (int)strlen(kv->value),
			              "%d.%d; kv->value_len(%d) == strlen(%s)(%d)",
			              j, i, kv->value_len, kv->value, (int)strlen(kv->value));
		}
	}

}
END_TEST

START_TEST(kvvec_test_free_null)
{
	kvvec_destroy(NULL, KVVEC_FREE_ALL);
}
END_TEST

START_TEST(kvvec_test_lookup_unsorted)
{
	struct kvvec *kvv;
	struct key_value *kv;
	kvv = kvvec_create(1);
	ck_assert(kvv != NULL);

	kvvec_addkv_str(kvv, "golf", "7");
	kvvec_addkv_str(kvv, "alfa", "1");
	kvvec_addkv_str(kvv, "echo", "5");
	kvvec_addkv_str(kvv, "foxtrot", "6");
	kvvec_addkv_str(kvv, "bravo", "2");
	kvvec_addkv_str(kvv, "hotel", "8");
	kvvec_addkv_str(kvv, "charlie", "3");
	kvvec_addkv_str(kvv, "delta", "4");

	ck_assert_int_eq(kvv->kv_pairs, 8);

	kv = kvvec_fetch(kvv, "foxtrot", strlen("foxtrot"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "foxtrot");
	ck_assert_str_eq(kv->value, "6");

	kv = kvvec_fetch(kvv, "hotel", strlen("hotel"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "hotel");
	ck_assert_str_eq(kv->value, "8");

	kv = kvvec_fetch(kvv, "delta", strlen("delta"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "delta");
	ck_assert_str_eq(kv->value, "4");

	kv = kvvec_fetch(kvv, "golf", strlen("golf"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "golf");
	ck_assert_str_eq(kv->value, "7");

	kv = kvvec_fetch(kvv, "fox", strlen("fox"));
	ck_assert(kv == NULL);

	kv = kvvec_fetch(kvv, "foxtrottrot", strlen("foxtrotrot"));
	ck_assert(kv == NULL);

	kvvec_destroy(kvv, 0);
}
END_TEST

START_TEST(kvvec_test_lookup_sorted)
{
	struct kvvec *kvv;
	struct key_value *kv;
	kvv = kvvec_create(1);
	ck_assert(kvv != NULL);

	kvvec_addkv_str(kvv, "golf", "7");
	kvvec_addkv_str(kvv, "alfa", "1");
	kvvec_addkv_str(kvv, "echo", "5");
	kvvec_addkv_str(kvv, "foxtrot", "6");
	kvvec_addkv_str(kvv, "bravo", "2");
	kvvec_addkv_str(kvv, "hotel", "8");
	kvvec_addkv_str(kvv, "charlie", "3");
	kvvec_addkv_str(kvv, "delta", "4");

	kvvec_sort(kvv);

	ck_assert_int_eq(kvv->kv_pairs, 8);

	kv = kvvec_fetch(kvv, "foxtrot", strlen("foxtrot"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "foxtrot");
	ck_assert_str_eq(kv->value, "6");

	kv = kvvec_fetch(kvv, "hotel", strlen("hotel"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "hotel");
	ck_assert_str_eq(kv->value, "8");

	kv = kvvec_fetch(kvv, "delta", strlen("delta"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "delta");
	ck_assert_str_eq(kv->value, "4");

	kv = kvvec_fetch(kvv, "golf", strlen("golf"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "golf");
	ck_assert_str_eq(kv->value, "7");

	kv = kvvec_fetch(kvv, "fox", strlen("fox"));
	ck_assert(kv == NULL);

	kv = kvvec_fetch(kvv, "foxtrottrot", strlen("foxtrottrot"));
	ck_assert(kv == NULL);

	kvvec_destroy(kvv, 0);
}
END_TEST


/**
 * This test verifies that the is_sorted flag triggers a binary search, which
 * reduces the overall time. Forcing a unsorted list to be reduced means some
 * values shouldn't be found.
 *
 * This shouldn't happen, since is_sorted should only be set if the list is
 * sorted on key.
 */
START_TEST(kvvec_test_lookup_sorted_uses_binary)
{
	struct kvvec *kvv;
	struct key_value *kv;
	kvv = kvvec_create(1);
	ck_assert(kvv != NULL);

	kvvec_addkv_str(kvv, "bravo", "2");
	kvvec_addkv_str(kvv, "charlie", "3");
	kvvec_addkv_str(kvv, "delta", "4");
	kvvec_addkv_str(kvv, "echo", "5");
	kvvec_addkv_str(kvv, "foxtrot", "6");
	kvvec_addkv_str(kvv, "golf", "7");
	kvvec_addkv_str(kvv, "hotel", "8");
	kvvec_addkv_str(kvv, "alfa", "1");

	/* Using non-sorted lookup alfa should be found */
	kv = kvvec_fetch(kvv, "alfa", strlen("alfa"));
	ck_assert(kv != NULL);
	ck_assert_str_eq(kv->key, "alfa");
	ck_assert_str_eq(kv->value, "1");

	/* Forcing sorted flag, binary search should be used */
	kvv->kvv_sorted = 1;

	/* alfa shouldn't be found, since binary search reduces to first half */
	kv = kvvec_fetch(kvv, "alfa", strlen("alfa"));
	ck_assert(kv == NULL);

	kvvec_destroy(kvv, 0);
}
END_TEST

Suite *kvvec_suite(void)
{
	Suite *s = suite_create("kvvec");

	TCase *tc;
	tc = tcase_create("kvvec");
	tcase_add_test(tc, kvvec_tests);
	tcase_add_test(tc, kvvec_test_free_null);
	tcase_add_test(tc, kvvec_test_lookup_unsorted);
	tcase_add_test(tc, kvvec_test_lookup_sorted);
	tcase_add_test(tc, kvvec_test_lookup_sorted_uses_binary);
	suite_add_tcase(s, tc);

	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = kvvec_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
