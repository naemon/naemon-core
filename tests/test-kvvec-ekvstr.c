#include <check.h>
#include <lib/kvvec_ekvstr.h>

/*
 * This tests to unpack then pack, and see if everything is back again.
 *
 * Because the previous test verifies that the data is unpacked correctly,
 * using that information, the data must here also be unpacked
 */
START_TEST(kvvec_ekvstr_consistancy)
{
	struct kvvec *kvv;
	char *test;
	char *res;
	int i;

	char *ekvstr[] = {
		/* Test string escaping */
		"ka\\=ka=pe\\=lle\\;;a=b;a\\\\=b",

		/* Test empty */
		"",

		/* Test single */
		"a=b",

		/* Test duplicate keys, and order */
		"a=b;a=c;a=d;a=c",

		/* Test duplicate keys, and order */
		"a=b;a=c;a=d;b=a;a=c;b=b",

		NULL
	};

	for (i = 0; ekvstr[i]; i++) {
		test = ekvstr[i];

		kvv = ekvstr_to_kvvec(test);
		ck_assert_msg(kvv != NULL, "Could not generate kvvec from %s", test);
		res = kvvec_to_ekvstr(kvv);
		ck_assert_str_eq(test, res);
		free(res);
		kvvec_destroy(kvv, KVVEC_FREE_ALL);
	}
}
END_TEST
/*
 * This test to unpack an kvvec, to verify escaping of the unpacked data.
 */
START_TEST(kvvec_ekvstr_unpack)
{
	struct kvvec *kvv;

	/* common alfanum */
	kvv = ekvstr_to_kvvec("abc=def");
	ck_assert(kvv != NULL);
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "abc");
	ck_assert_str_eq(kvv->kv[0].value, "def");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	/* multiple pairs alfanum */
	kvv = ekvstr_to_kvvec("abc=def;ghi=jkl;mno=pqr");
	ck_assert(kvv != NULL);
	ck_assert_msg(kvv->kv_pairs == 3, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "abc");
	ck_assert_str_eq(kvv->kv[0].value, "def");
	ck_assert_str_eq(kvv->kv[1].key, "ghi");
	ck_assert_str_eq(kvv->kv[1].value, "jkl");
	ck_assert_str_eq(kvv->kv[2].key, "mno");
	ck_assert_str_eq(kvv->kv[2].value, "pqr");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	/* empty */
	kvv = ekvstr_to_kvvec("");
	ck_assert(kvv != NULL);
	ck_assert_msg(kvv->kv_pairs == 0, "Invalid pair count");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	/* quote escaping */
	kvv = ekvstr_to_kvvec("\\=\\;\\=\\=\\;=\\=\\;\\;\\=");
	ck_assert(kvv != NULL);
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "=;==;");
	ck_assert_str_eq(kvv->kv[0].value, "=;;=");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	/* backslash escaping */
	kvv = ekvstr_to_kvvec("\\\\abc=\\\\def");
	ck_assert(kvv != NULL);
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "\\abc");
	ck_assert_str_eq(kvv->kv[0].value, "\\def");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	/* only escape char */
	kvv = ekvstr_to_kvvec("\\");
	ck_assert(kvv == NULL);

	kvv = ekvstr_to_kvvec("abc\\");
	ck_assert(kvv == NULL);

	kvv = ekvstr_to_kvvec("a;b=c");
	ck_assert(kvv == NULL);

	kvv = ekvstr_to_kvvec("a\\;b=c");
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "a;b");
	ck_assert_str_eq(kvv->kv[0].value, "c");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	kvv = ekvstr_to_kvvec("=");
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "");
	ck_assert_str_eq(kvv->kv[0].value, "");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	kvv = ekvstr_to_kvvec("==");
	ck_assert(kvv == NULL);

	kvv = ekvstr_to_kvvec("===");
	ck_assert(kvv == NULL);

	kvv = ekvstr_to_kvvec(";");
	ck_assert(kvv == NULL);

	kvv = ekvstr_to_kvvec("=;");
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "");
	ck_assert_str_eq(kvv->kv[0].value, "");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	kvv = ekvstr_to_kvvec("\\==\\=");
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "=");
	ck_assert_str_eq(kvv->kv[0].value, "=");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	/* Trailing escape char is ignored */
	kvv = ekvstr_to_kvvec("abc=def\\");
	ck_assert(kvv != NULL);
	ck_assert_msg(kvv->kv_pairs == 1, "Invalid pair count");
	ck_assert_str_eq(kvv->kv[0].key, "abc");
	ck_assert_str_eq(kvv->kv[0].value, "def");
	kvvec_destroy(kvv, KVVEC_FREE_ALL);
}
END_TEST

/*
 * Test to pack a string with one byte at a time, and see that all 256 values can be stored as a kvv
 */
START_TEST(kvvec_ekvstr_escaping)
{
	struct kvvec *kvv = ekvstr_to_kvvec("a=b;c=d"); /* To kickstart */
	struct kvvec *kvvb = NULL;
	char *buf;
	unsigned int i;
	ck_assert_msg(kvv != NULL, "Could not parse kickstart-string");
	ck_assert_msg(kvv->kv_pairs == 2, "Incorrect length of kickstart-string");

	for (i = 0; i < 256; i++) {
		kvv->kv[0].value[0] = i;
		buf = kvvec_to_ekvstr(kvv);
		ck_assert_msg(buf != NULL, "Could not ekvstr-encode kvvec for %d (%c)", i, i);
		kvvb = ekvstr_to_kvvec(buf);
		free(buf);
		ck_assert_msg(kvvb != NULL, "Could parse kvvec for %d (%c), String: %s", i, i, buf);
		ck_assert_msg(kvvb->kv_pairs == 2, "Incorrect length of kvvec for %d (%c), String: %s", i, i, buf);
		ck_assert_str_eq(kvvb->kv[0].key, "a");
		ck_assert_msg(kvvb->kv[0].value_len == 1,
		              "Incorrect value length in kvvec for %d (%c), String: %s", i, i, buf);
		ck_assert_msg((unsigned char)kvvb->kv[0].value[0] == i,
		              "Incorrect value in kvvec for %d (%c), String: %s", i, i, buf);
		ck_assert_str_eq(kvvb->kv[1].key, "c");
		ck_assert_str_eq(kvvb->kv[1].value, "d");
		kvvec_destroy(kvvb, KVVEC_FREE_ALL);
	}
	kvvec_destroy(kvv, KVVEC_FREE_ALL);
}
END_TEST

Suite *kvvec_ekvstr_suite(void)
{
	Suite *s = suite_create("Escaped KVvec");

	TCase *tc;
	tc = tcase_create("Escaped KVvec");
	tcase_add_test(tc, kvvec_ekvstr_consistancy);
	tcase_add_test(tc, kvvec_ekvstr_unpack);
	tcase_add_test(tc, kvvec_ekvstr_escaping);
	suite_add_tcase(s, tc);

	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = kvvec_ekvstr_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
