#include <check.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <float.h>
#include "naemon/nm_writebuf.h"

/*
 * The state files must not change, byte for byte: whatever the appenders
 * write has to be exactly what printf() would have produced.
 */

#define BUFSZ (1 << 20)

static struct nm_writebuf wb;
static char backing[BUFSZ];

static void wb_reset(void)
{
	/* fd -1: nothing is ever flushed, the buffer is inspected directly */
	wb.buf = backing;
	wb.cap = sizeof(backing);
	wb.len = 0;
	wb.fd = -1;
	wb.error = 0;
}

static void ck_dbl_matches_snprintf(const char *fmt, double v)
{
	/* %f of DBL_MAX is 316 characters; the oracle must never truncate */
	char expected[512];
	int n = snprintf(expected, sizeof(expected), fmt, v);

	ck_assert_int_lt(n, (int)sizeof(expected));

	wb_reset();
	nm_wb_dbl(&wb, fmt, v);
	ck_assert_int_eq((int)wb.len, n);
	ck_assert(memcmp(wb.buf, expected, (size_t)n) == 0);
}

START_TEST(dbl_matches_snprintf_over_many_values)
{
	static const char *fmts[] = {"%f", "%.2f", "%.3f"};
	unsigned f, i;

	for (f = 0; f < sizeof(fmts) / sizeof(fmts[0]); f++) {
		for (i = 0; i < 5000; i++) {
			ck_dbl_matches_snprintf(fmts[f], (double)i);
			ck_dbl_matches_snprintf(fmts[f], i / 1000.0);
			ck_dbl_matches_snprintf(fmts[f], -(i / 7.0));
		}
	}
}
END_TEST

START_TEST(dbl_handles_edge_values)
{
	ck_dbl_matches_snprintf("%f", 0.0);
	ck_dbl_matches_snprintf("%f", -0.0);
	ck_dbl_matches_snprintf("%f", -1.0);
	ck_dbl_matches_snprintf("%.2f", 0.005);
	ck_dbl_matches_snprintf("%.3f", 1e12);
	ck_dbl_matches_snprintf("%.3f", -1e12);
	ck_dbl_matches_snprintf("%f", 1e30);
	ck_dbl_matches_snprintf("%f", -1e30);
	/* longer than nm_wb_printf()'s stack buffer: must not be truncated */
	ck_dbl_matches_snprintf("%f", 1e60);
	ck_dbl_matches_snprintf("%.3f", -1e60);
	ck_dbl_matches_snprintf("%f", 1e300);
	ck_dbl_matches_snprintf("%f", DBL_MAX);
	ck_dbl_matches_snprintf("%f", -DBL_MAX);
}
END_TEST

START_TEST(int_writers_match_printf)
{
	char expected[32];
	long long vals[] = {0, 1, -1, 9, 10, -10, 99999, -99999,
	                    LLONG_MAX, LLONG_MIN};
	unsigned i;
	int n;

	for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		n = snprintf(expected, sizeof(expected), "%lld", vals[i]);
		wb_reset();
		nm_wb_int(&wb, vals[i]);
		ck_assert_int_eq((int)wb.len, n);
		ck_assert(memcmp(wb.buf, expected, (size_t)n) == 0);
	}

	n = snprintf(expected, sizeof(expected), "%llu", ULLONG_MAX);
	wb_reset();
	nm_wb_uint(&wb, ULLONG_MAX);
	ck_assert_int_eq((int)wb.len, n);
	ck_assert(memcmp(wb.buf, expected, (size_t)n) == 0);
}
END_TEST

START_TEST(kv_macros_produce_key_value_lines)
{
	wb_reset();
	nm_wb_kv_str(&wb, "name", "value");
	nm_wb_kv_int(&wb, "count", -7);
	nm_wb_kv_uint(&wb, "size", 42u);
	nm_wb_kv_dbl(&wb, "ratio", "%.2f", 0.5);
	ck_assert(wb.len < sizeof(backing));
	wb.buf[wb.len] = '\0';
	ck_assert_str_eq(wb.buf, "name=value\ncount=-7\nsize=42\nratio=0.50\n");
}
END_TEST

/* nm_wb_str() must not walk off the end when handed NULL. */
START_TEST(str_accepts_null)
{
	wb_reset();
	nm_wb_kv_str(&wb, "empty", NULL);
	wb.buf[wb.len] = '\0';
	ck_assert_str_eq(wb.buf, "empty=\n");
}
END_TEST

int main(void)
{
	int number_failed;
	Suite *s = suite_create("nm_writebuf");
	TCase *tc = tcase_create("formatting");
	SRunner *sr;

	tcase_set_timeout(tc, 60);
	tcase_add_test(tc, dbl_matches_snprintf_over_many_values);
	tcase_add_test(tc, dbl_handles_edge_values);
	tcase_add_test(tc, int_writers_match_printf);
	tcase_add_test(tc, kv_macros_produce_key_value_lines);
	tcase_add_test(tc, str_accepts_null);
	suite_add_tcase(s, tc);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
