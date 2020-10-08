#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "naemon/nm_arith.h"

START_TEST(addition_overflow)
{
	long dest;
	int i;
	ck_assert(!nm_arith_saddl_overflow(LONG_MAX, LONG_MAX, &dest));
	ck_assert(!nm_arith_saddl_overflow(14, LONG_MAX, &dest));
	ck_assert(!nm_arith_saddl_overflow(LONG_MAX, 14, &dest));
	ck_assert(!nm_arith_saddl_overflow(-LONG_MAX, -LONG_MAX, &dest));
	ck_assert(!nm_arith_saddl_overflow(-888, -LONG_MAX, &dest));
	ck_assert(!nm_arith_saddl_overflow(-LONG_MAX, -7666, &dest));

	ck_assert(nm_arith_saddl_overflow(-15, -7666, &dest));
	ck_assert_int_eq(-7681, dest);

	ck_assert(nm_arith_saddl_overflow(99995, -7666, &dest));
	ck_assert_int_eq(92329, dest);

	ck_assert(nm_arith_saddl_overflow(99999, 999999, &dest));
	ck_assert_int_eq(1099998, dest);

	ck_assert(nm_arith_saddl_overflow(0, LONG_MAX, &dest));
	ck_assert_int_eq(LONG_MAX, dest);

	ck_assert(nm_arith_saddl_overflow(LONG_MIN, 0, &dest));
	ck_assert_int_eq(LONG_MIN, dest);

	for (i = 0; i < 10000; ++i) {
		long a, b;
		a = random();
		b = random();
		if (random() % 2 == 0) {
			a *= -1;
		}
		if (random() % 2 == 0) {
			b *= -1;
		}

		bool success = nm_arith_saddl_overflow(a, b, &dest);
		if (success)
			ck_assert_int_eq(dest, a + b);
		else
			ck_assert_int_ne(dest, a + b);
	}
}
END_TEST

START_TEST(subtraction_overflow)
{
	long dest;
	int i;
	ck_assert(!nm_arith_ssubl_overflow(0, LONG_MIN, &dest));
	ck_assert(nm_arith_ssubl_overflow(0, LONG_MAX, &dest));
	ck_assert_int_eq(-LONG_MAX, dest);

	ck_assert(nm_arith_ssubl_overflow(1234, 0, &dest));
	ck_assert_int_eq(1234, dest);

	ck_assert(nm_arith_ssubl_overflow(0, 0, &dest));
	ck_assert_int_eq(0, dest);

	for (i = 0; i < 10000; ++i) {
		long a, b;
		a = random();
		b = random();
		if (random() % 2 == 0) {
			a *= -1;
		}
		if (random() % 2 == 0) {
			b *= -1;
		}

		bool success = nm_arith_ssubl_overflow(a, b, &dest);
		if (success)
			ck_assert_int_eq(dest, a - b);
		else
			ck_assert_int_ne(dest, a - b);
	}
}
END_TEST

START_TEST(multiplication_overflow)
{
	long a, b, dest = 0;
	int i;
	ck_assert(nm_arith_smull_overflow(0, 0, &dest));
	ck_assert_int_eq(0, dest);

	ck_assert(nm_arith_smull_overflow(0, 1234, &dest));
	ck_assert_int_eq(0, dest);

	ck_assert(nm_arith_smull_overflow(1234, 0, &dest));
	ck_assert_int_eq(0, dest);

	ck_assert(nm_arith_smull_overflow(-1234, 0, &dest));
	ck_assert_int_eq(0, dest);

	ck_assert(nm_arith_smull_overflow(0, -1234, &dest));
	ck_assert_int_eq(0, dest);

	for (i = 0; i < 10000; ++i) {
		a = random();
		b = random();
		if (random() % 2 == 0) {
			a *= -1;
		}
		if (random() % 2 == 0) {
			b *= -1;
		}
		bool success = nm_arith_smull_overflow(a, b, &dest);
		if (success)
			ck_assert_int_eq(dest, a * b);
		else
			ck_assert_int_ne(dest, a * b);
	}
}
END_TEST

Suite *
arithmetic_suite(void)
{
	Suite *s = suite_create("Arithmetics");
	TCase *tc_overflow_detection = tcase_create("Arithmetics overflow detection");
	tcase_add_test(tc_overflow_detection, addition_overflow);
	tcase_add_test(tc_overflow_detection, subtraction_overflow);
	tcase_add_test(tc_overflow_detection, multiplication_overflow);
	suite_add_tcase(s, tc_overflow_detection);
	return s;
}

int main(void)
{
	int number_failed = 0;
	unsigned int seed = time(NULL);
	Suite *s = arithmetic_suite();
	SRunner *sr = srunner_create(s);
	srandom(seed);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
