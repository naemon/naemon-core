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

#include "naemon/utils.c"
#include "naemon/configuration.h"
#include "naemon/defaults.h"
#include "tap.h"

int main(int argc, char **argv)
{
	int result;
	int c = 0;
	time_t current_time;
	time_t test_time;
	time_t saved_test_time;
	time_t next_valid_time = 0L;
	time_t chosen_valid_time = 0L;
	timeperiod *temp_timeperiod = NULL;
	int is_valid_time = 0;
	int iterations = 1000;

	plan_tests(6046);

	/* reset program variables */
	reset_variables();

	printf("Reading configuration data...\n");

	config_file = strdup(get_default_config_file());
	/* read in the configuration files (main config file, resource and object config files) */
	result = read_main_config_file(config_file);
	ok(result == OK, "Read main configuration file okay - if fails, use nagios -v to check");

	result = read_all_object_data(config_file);
	ok(result == OK, "Read all object config files");

	result = pre_flight_check();
	ok(result == OK, "Preflight check okay");

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

	putenv("TZ=UTC");
	tzset();
	test_time = saved_test_time;
	c = 0;
	while (c < iterations) {
		is_valid_time = check_time_against_period(test_time, temp_timeperiod);
		ok(is_valid_time == OK, "Always OK for 24x7 with TZ=UTC, time_t=%lu", test_time);
		chosen_valid_time = 0L;
		_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
		ok(test_time == chosen_valid_time, "get_next_valid_time always returns same time");
		test_time += 1800;
		c++;
	}

	putenv("TZ=Europe/London");
	tzset();
	test_time = saved_test_time;
	c = 0;
	while (c < iterations) {
		is_valid_time = check_time_against_period(test_time, temp_timeperiod);
		ok(is_valid_time == OK, "Always OK for 24x7 with TZ=Europe/London, time_t=%lu", test_time);
		_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
		ok(test_time == chosen_valid_time, "get_next_valid_time always returns same time, time_t=%lu", test_time);
		test_time += 1800;
		c++;
	}

	/* 2009-11-01 is the day when clocks go back an hour in America. Bug happens during 23:00 to 00:00 */
	/* This is 23:01:01 */
	saved_test_time = 1256511661;
	saved_test_time = saved_test_time - (24 * 60 * 60);

	putenv("TZ=America/New_York");
	tzset();
	test_time = saved_test_time;
	c = 0;
	while (c < iterations) {
		is_valid_time = check_time_against_period(test_time, temp_timeperiod);
		ok(is_valid_time == OK, "Always OK for 24x7 with TZ=America/New_York, time_t=%lu", test_time);
		_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
		ok(test_time == chosen_valid_time, "get_next_valid_time always returns same time, time_t=%lu", test_time);
		test_time += 1800;
		c++;
	}



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

	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1288103400, "Next valid time should be Tue Oct 26 16:30:00 2010, was %s", ctime(&chosen_valid_time));


	temp_timeperiod = find_timeperiod("Test_exclude2");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod 2");
	test_time = 1278939600; //mon jul 12 15:00:00
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "12 Jul 2010 15:00:00 should not be valid");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1279058340, "Next valid time should be Tue Jul 13 23:59:00 2010, was %s", ctime(&chosen_valid_time));


	temp_timeperiod = find_timeperiod("Test_exclude3");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod 3");
	test_time = 1278939600; //mon jul 12 15:00:00
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "12 Jul 2010 15:00:00 should not be valid");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1284474600, "Next valid time should be Tue Sep 14 16:30:00 2010, was %s", ctime(&chosen_valid_time));


	temp_timeperiod = find_timeperiod("Test_exclude4");
	ok(temp_timeperiod != NULL, "Testing Exclude timeperiod 4");
	test_time = 1278939600; //mon jul 12 15:00:00
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "12 Jul 2010 15:00:00 should not be valid");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1283265000, "Next valid time should be Tue Aug 31 16:30:00 2010, was %s", ctime(&chosen_valid_time));

	temp_timeperiod = find_timeperiod("exclude_always");
	ok(temp_timeperiod != NULL, "Testing exclude always");
	test_time = 1278939600; //mon jul 12 15:00:00
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "12 Jul 2010 15:00:00 should not be valid");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == test_time, "There should be no next valid time, was %s", ctime(&chosen_valid_time));


	/* Back to New york */
	putenv("TZ=America/New_York");
	tzset();


	temp_timeperiod = find_timeperiod("sunday_only");
	ok(temp_timeperiod != NULL, "Testing Sunday 00:00-01:15,03:15-22:00");
	putenv("TZ=Europe/London");
	tzset();


	test_time = 1256421000;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Sat Oct 24 22:50:00 2009 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1256425200, "Next valid time=Sun Oct 25 00:00:00 2009");


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

	test_time = 1256429700;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "Sun Oct 25 01:15:00 2009 - true");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == test_time, "Next valid time=Sun Oct 25 01:15:00 2009");

	test_time = 1256430400;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Sun Oct 25 01:26:40 2009 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1256440500, "Next valid time should be Sun Oct 25 03:15:00 2009, was %s", ctime(&chosen_valid_time));

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

	test_time = 1256508000;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == OK, "Sun Oct 25 22:00:00 2009 - true");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1256508000, "Next valid time=Sun Oct 25 22:00:00 2009");

	test_time = 1256508001;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Sun Oct 25 22:00:01 2009 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1257033600, "Next valid time=Sun Nov 1 00:00:00 2009");

	test_time = 1256513000;
	is_valid_time = check_time_against_period(test_time, temp_timeperiod);
	ok(is_valid_time == ERROR, "Sun Oct 25 23:23:20 2009 - false");
	_get_next_valid_time(test_time, &chosen_valid_time, temp_timeperiod);
	ok(chosen_valid_time == 1257033600, "Next valid time=Sun Nov 1 00:00:00 2009");




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

	my_free(config_file);

	return exit_status();
}
