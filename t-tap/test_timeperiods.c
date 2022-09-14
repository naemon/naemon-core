/*****************************************************************************
 *
 * test_timeperiod.c - Test timeperiod
 *
 * Program: Naemon Core Testing
 * License: GPL
 *
 * Description:
 *
 * Tests Naemon configuration loading
 *
 * License:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/
#include <string.h>

#include "naemon/objects_timeperiod.c"
#include "naemon/utils.h"
#include "naemon/configuration.h"
#include "naemon/defaults.h"
#include "naemon/globals.h"
#include "tap.h"

static void noeol_ctime(const time_t *when, char *buf)
{
	ctime_r(when, buf);
	buf[strlen(buf) - 1] = 0;
}

static struct timeperiod *test_get_timeperiod(const char *name)
{
	struct timeperiod *tp;
	tp = find_timeperiod(name);
	if (!tp) {
		printf("CRITICAL: Failed to find timeperiod '%s'\n", name);
		exit(1);
	}
	return tp;
}

#define test_check_time_against_period(expect, when, tp_name) \
	do { \
		int ret; \
		char buf[32]; \
		time_t t_when = when; \
		struct timeperiod *tp; \
		tp = test_get_timeperiod(tp_name); \
		ret = check_time_against_period(when, tp); \
		noeol_ctime(&t_when, buf); \
		ok(ret == expect, "Expected %d, got %d for %s in '%s' (TZ=%s)", \
		   expect, ret, buf, tp->name, getenv("TZ")); \
	} while (0)

#define test_get_next_valid_time(expect, when, tp_name) \
	do { \
		time_t chosen, t_when, t_expect; \
		char ct_expect[32], ct_chosen[32], ct_when[32]; \
		struct timeperiod *tp; \
		tp = test_get_timeperiod(tp_name); \
		t_when = when; \
		t_expect = expect; \
		_get_next_valid_time(t_when, &chosen, tp); \
		noeol_ctime(&chosen, ct_chosen); \
		noeol_ctime(&t_when, ct_when); \
		noeol_ctime(&t_expect, ct_expect); \
		ok(t_expect == chosen, "GNV: Tested %lu (%s) against '%s'. Got %lu (%s). Expected %lu (%s)", \
		   t_when, ct_when, tp->name, chosen, ct_chosen, t_expect, ct_expect); \
	} while (0)

#define test_get_next_invalid_time(expect, when, tp_name) \
	do { \
		time_t chosen = 0, t_expect = expect, t_when = when; \
		char ct_expect[32], ct_chosen[32], ct_when[32]; \
		struct timeperiod *tp; \
		tp = test_get_timeperiod(tp_name); \
		_get_next_invalid_time(when, &chosen, tp); \
		noeol_ctime(&chosen, ct_chosen); \
		noeol_ctime(&t_when, ct_when); \
		noeol_ctime(&t_expect, ct_expect); \
		ok(expect == chosen, "GNI: Tested %lu (%s) against '%s'. Got %lu (%s). Expected %lu (%s)", \
		   when, ct_when, tp->name, chosen, ct_chosen, t_expect, ct_expect); \
	} while (0)

#define test_get_matching_timerange(expect, when_num, tp_name) \
	do { \
		timerange *range; \
		struct timeperiod *tp; \
		int count = 0; \
		time_t when = when_num; \
		char buf[32]; \
		noeol_ctime(&when, buf); \
		tp = test_get_timeperiod(tp_name); \
		range = _get_matching_timerange(when, tp); \
		for (; range; range = range->next) { \
			count++; \
		} \
		ok(count == expect, "%d/%d range entries in %s for %lu - %s", count, expect, tp->name, when, buf); \
	} while (0)

struct expected_range {
	int start, end;
};

int main(int argc, char **argv)
{
	int result;
	time_t current_time;
	time_t test_time;
	time_t saved_test_time;
	time_t next_valid_time = 0L;
	time_t chosen_valid_time = 0L;
	timeperiod *temp_timeperiod = NULL;
	int is_valid_time = 0;

	int c = 0;
	int iterations = 1000;
	int failures;

	plan_tests(121);


	/* reset program variables */
	reset_variables();

	printf("Reading configuration data...\n");

	config_file = strdup(TESTDIR "naemon.cfg");
	config_file_dir = nspath_absolute_dirname(config_file, NULL);
	/* read in the configuration files (main config file, resource and object config files) */
	result = read_main_config_file(config_file);
	ok(result == OK, "Read main configuration file okay - if fails, use nagios -v to check");

	result = read_all_object_data(config_file);
	ok(result == OK, "Read all object config files");

	result = pre_flight_check();
	ok(result == OK, "Preflight check okay");

	/* make sure system timezone doesn't interfere with our tests */
	putenv("TZ=UTC");
	tzset();

	test_time = 1280579600; /* Sat Jul 31 14:33:20 CEST 2010 */
	test_check_time_against_period(OK, test_time, "myexclude");

	/* test "normal" timeperiods */
	/* even-hours-until-0500 has 00-01, 02-03 and 04-05 */
	test_check_time_against_period(OK, 0, "even-hours-until-0500");
	test_check_time_against_period(OK, 1800, "even-hours-until-0500");
	test_check_time_against_period(ERROR, 3600, "even-hours-until-0500");
	test_check_time_against_period(ERROR, 5400, "even-hours-until-0500");
	test_check_time_against_period(OK, 7200, "even-hours-until-0500");
	test_check_time_against_period(OK, 9000, "even-hours-until-0500");
	test_check_time_against_period(ERROR, 10800, "even-hours-until-0500");
	test_check_time_against_period(OK, 14400, "even-hours-until-0500");
	test_check_time_against_period(OK, 17999, "even-hours-until-0500");
	test_get_next_valid_time(1, 1, "even-hours-until-0500");
	test_get_next_valid_time(7200, 3600, "even-hours-until-0500");
	test_get_next_valid_time(14400, 10800, "even-hours-until-0500");
	test_get_next_valid_time(17999, 17999, "even-hours-until-0500");
	test_get_next_valid_time(86400, 18000, "even-hours-until-0500");
	test_get_next_invalid_time(3600, 1, "even-hours-until-0500");
	test_get_next_invalid_time(10800, 7200, "even-hours-until-0500");
	test_get_next_invalid_time(10800, 10800, "even-hours-until-0500");
	test_get_next_invalid_time(18000, 14400, "even-hours-until-0500");
	test_get_next_invalid_time(18000, 18000, "even-hours-until-0500");

	/* test exclusions */
	test_check_time_against_period(ERROR, 0, "exclude-even-hours-until-0500");
	test_check_time_against_period(ERROR, 1800, "exclude-even-hours-until-0500");
	test_check_time_against_period(OK, 3600, "exclude-even-hours-until-0500");
	test_check_time_against_period(OK, 5400, "exclude-even-hours-until-0500");
	test_check_time_against_period(ERROR, 7200, "exclude-even-hours-until-0500");
	test_check_time_against_period(ERROR, 9000, "exclude-even-hours-until-0500");
	test_check_time_against_period(OK, 10800, "exclude-even-hours-until-0500");
	test_check_time_against_period(ERROR, 14400, "exclude-even-hours-until-0500");
	test_check_time_against_period(ERROR, 17999, "exclude-even-hours-until-0500");
	test_check_time_against_period(OK, 18000, "exclude-even-hours-until-0500");
	test_get_next_invalid_time(1, 1, "exclude-even-hours-until-0500");
	test_get_next_invalid_time(1800, 1800, "exclude-even-hours-until-0500");
	test_get_next_invalid_time(14400, 10800, "exclude-even-hours-until-0500");
	test_get_next_invalid_time(17999, 17999, "exclude-even-hours-until-0500");
	test_get_next_invalid_time(86400, 18000, "exclude-even-hours-until-0500");
	test_get_next_valid_time(3600, 1, "exclude-even-hours-until-0500");
	test_get_next_valid_time(10800, 7200, "exclude-even-hours-until-0500");
	test_get_next_valid_time(10800, 10800, "exclude-even-hours-until-0500");
	test_get_next_valid_time(18000, 14400, "exclude-even-hours-until-0500");
	test_get_next_valid_time(18000, 18000, "exclude-even-hours-until-0500");

	/* test exact-date exceptions */
	test_time = 1405814400; /* Sun Jul 20 00:00:00 UTC 2014 */
	test_get_matching_timerange(3, test_time, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(OK, test_time, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(OK, test_time + 1800, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(ERROR, test_time + 3600, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(ERROR, test_time + 5400, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(OK, test_time + 7200, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(OK, test_time + 9000, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(ERROR, test_time + 10800, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(OK, test_time + 14400, "sun-jul-20-even-hours-until-0500");
	test_check_time_against_period(OK, test_time + 17999, "sun-jul-20-even-hours-until-0500");

	/* test skip-day exceptions */
	test_time = 1405814400; /* Sun Jul 20 00:00:00 UTC 2014 */
	test_get_matching_timerange(0, test_time, "monday3-thursday4_10-12_14-16");
	test_get_matching_timerange(2, test_time + 86400, "monday3-thursday4_10-12_14-16");

	/* testing 'use' and simple 'exclude' */
	test_check_time_against_period(OK, 123, "non-workhours");
	test_check_time_against_period(ERROR, 123, "workhours");
	test_get_next_valid_time(25200, 123, "workhours");
	test_get_next_valid_time(64800, 25200, "non-workhours");

	/* Make sure we avoid infinite iterations */
	temp_timeperiod = find_timeperiod("none");
	test_check_time_against_period(ERROR, 123, "none");
	test_get_next_invalid_time(123, 123, "none");
	test_get_next_valid_time(123, 123, "none");
	test_check_time_against_period(OK, 123, "24x7");
	test_get_next_invalid_time(123, 123, "24x7");


	/* Timeperiod exclude tests, from Jean Gabes */
	temp_timeperiod = find_timeperiod("weekly_complex");
	test_time = 248234; /* Sat Jan 3 20:57:14 UTC */
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "%lu should be valid in weekly_complex", test_time);
	test_get_next_invalid_time(252000, 252000, "weekly_complex");
	test_get_next_valid_time(255600, 252000, "weekly_complex");

	temp_timeperiod = find_timeperiod("Test_exclude");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod");
	test_time = 1278939600; //mon jul 12 15:00:00
	test_check_time_against_period(ERROR, test_time, "Test_exclude");
	test_get_next_invalid_time(1278939600, test_time, "Test_exclude");
	test_get_next_valid_time(1288110600, test_time, "Test_exclude");

	time(&current_time);
	test_time = current_time;
	saved_test_time = current_time;

	temp_timeperiod = find_timeperiod("none");
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "No valid time because time period is empty");

	get_next_valid_time(current_time, &next_valid_time, temp_timeperiod);
	ok(current_time == next_valid_time, "There is no valid time due to timeperiod");

	temp_timeperiod = find_timeperiod("24x7");

	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "Fine because 24x7");

	get_next_valid_time(current_time, &next_valid_time, temp_timeperiod);
	ok((next_valid_time - current_time) <= 2, "Next valid time should be the current_time, but with a 2 second tolerance");


	/* 2009-10-25 is the day when clocks go back an hour in Europe. Bug happens during 23:00 to 00:00 */
	/* This is 23:01:01 */
	saved_test_time = 1256511661;
	saved_test_time = saved_test_time - (24 * 60 * 60);

	/*
	 * To find out what has failed, run gdb for this test case
	 * (libtool --mode=execute gdb t-tap/test_timeperiods) and type
	 * "watch failures", then it will interrupt on increment
	 */

	putenv("TZ=UTC");
	tzset();
	test_time = saved_test_time;
	failures = 0;
	for (c = 0; c < iterations; c++) {
		is_valid_time = check_time_against_period(test_time, temp_timeperiod);
		if (is_valid_time != OK)
			failures++;
		chosen_valid_time = 0L;
		_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
		if (test_time != chosen_valid_time)
			failures++;
		test_time += 1800;
	}
	ok(failures == 0, "24x7 with TZ=UTC");

	putenv("TZ=Europe/London");
	tzset();
	test_time = saved_test_time;
	failures = 0;
	for (c = 0; c < iterations; c++) {
		is_valid_time = check_time_against_period(test_time, temp_timeperiod);
		if (is_valid_time != OK)
			failures++;
		_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
		if (test_time != chosen_valid_time)
			failures++;
		test_time += 1800;
	}
	ok(failures == 0, "24x7 with TZ=Europe/London");

	/* 2009-11-01 is the day when clocks go back an hour in America. Bug happens during 23:00 to 00:00 */
	/* This is 23:01:01 */
	saved_test_time = 1256511661;
	saved_test_time = saved_test_time - (24 * 60 * 60);

	putenv("TZ=America/New_York");
	tzset();
	test_time = saved_test_time;
	failures = 0;
	for (c = 0; c < iterations; c++) {
		is_valid_time = check_time_against_period(test_time, temp_timeperiod);
		if (is_valid_time != OK)
			failures++;
		_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
		if (test_time != chosen_valid_time)
			failures++;
		test_time += 1800;
	}
	ok(failures == 0, "24x7 with TZ=America/New_York");


	/* Tests around clock change going back for TZ=Europe/London. 1256511661 = Sun Oct
	25 23:01:01 2009 */
	/* A little trip to Paris*/
	putenv("TZ=Europe/Paris");
	tzset();


	/* Timeperiod exclude tests, from Jean Gabes */
	temp_timeperiod = find_timeperiod("Test_exclude");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod");
	test_time = 1278939600; //mon jul 12 15:00:00
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "12 Jul 2010 15:00:00 should not be valid");
	test_get_next_valid_time(1288103400, test_time, "Test_exclude");


	temp_timeperiod = find_timeperiod("Test_exclude2");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod 2");
	test_time = 1278939600; //mon jul 12 15:00:00
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "12 Jul 2010 15:00:00 should not be valid");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1279058280, "Next valid time should be Tue Jul 13 23:58:00 2010, was %s", ctime(&chosen_valid_time));


	temp_timeperiod = find_timeperiod("Test_exclude3");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod 3");
	test_time = 1278939600; //mon jul 12 15:00:00
	test_check_time_against_period(ERROR, test_time, "Test_exclude");
	test_get_next_valid_time(1288103400, test_time, "Test_exclude");


	temp_timeperiod = find_timeperiod("Test_exclude4");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod 4");
	test_time = 1278939600; //mon jul 12 15:00:00
	test_check_time_against_period(ERROR, test_time, "Test_exclude4");
	test_get_next_invalid_time(1281996000, 1281065000, "myexclude4");
	test_get_next_valid_time(1283265000, 1278939600, "Test_exclude4");
	test_get_next_valid_time(1283265000, 1278939600, "Test_exclude4");

	temp_timeperiod = find_timeperiod("exclude_always");
	ok(temp_timeperiod != NULL, "Testing exclude always");
	test_time = 1278939600; //mon jul 12 15:00:00
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "12 Jul 2010 15:00:00 should not be valid");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == test_time, "There should be no next valid time, was %s", ctime(&chosen_valid_time));



	temp_timeperiod = find_timeperiod("sunday_only");
	ok(temp_timeperiod != NULL, "Testing Sunday 00:00-01:15,03:15-22:00");
	putenv("TZ=Europe/London");
	tzset();

	test_time = 1256421000;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Sat Oct 24 22:50:00 2009 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1256425200, "Next valid time=Sun Oct 25 00:00:00 2009, was %s", ctime(&chosen_valid_time));


	test_time = 1256421661;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Sat Oct 24 23:01:01 2009 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1256425200, "Next valid time=Sun Oct 25 00:00:00 2009");

	test_time = 1256425400;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "Sun Oct 25 00:03:20 2009 - true");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == test_time, "Next valid time=Sun Oct 25 00:03:20 2009");

	test_time = 1256429699;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "Sun Oct 25 01:14:59 2009 - true");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == test_time, "Next valid time=Sun Oct 25 01:15:00 2009");

	/* daylight savings time tests */
	test_time = 1256429700;
	test_check_time_against_period(ERROR, test_time, "sunday_only");
	test_get_next_valid_time(1256440500, test_time, "sunday_only");

	test_time = 1256430400;
	test_check_time_against_period(ERROR, test_time, "sunday_only");
	test_get_next_valid_time(1256440500, test_time, "sunday_only");

	test_time = 1256440500;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "Sun Oct 25 03:15:00 2009 - true");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == test_time, "Next valid time=Sun Oct 25 03:15:00 2009");

	test_time = 1256500000;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "Sun Oct 25 19:46:40 2009 - true");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1256500000, "Next valid time=Sun Oct 25 19:46:40 2009");

	test_time = 1256507999;
	test_check_time_against_period(OK, test_time, "sunday_only");
	test_get_next_valid_time(test_time, test_time, "sunday_only");

	test_time = 1256508001;
	test_check_time_against_period(ERROR, test_time, "sunday_only");
	test_get_next_valid_time(1257033600, test_time, "sunday_only");

	test_time = 1256513000;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Sun Oct 25 23:23:20 2009 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1257033600, "Next valid time=Sun Nov 1 00:00:00 2009, was %lu : %s", chosen_valid_time, ctime(&chosen_valid_time));




	temp_timeperiod = find_timeperiod("weekly_complex");
	ok(temp_timeperiod != NULL, "Testing complex weekly timeperiod definition");
	putenv("TZ=America/New_York");
	tzset();

	test_time = 1268109420;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Mon Mar  8 23:37:00 2010 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1268115300, "Next valid time=Tue Mar  9 01:15:00 2010");





	cleanup();

	nm_free(config_file);

	return exit_status();
}
