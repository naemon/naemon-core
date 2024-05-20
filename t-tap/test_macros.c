/*****************************************************************************
 *
 * test_macros.c - Test macro expansion and escaping
 *
 * Program: Nagios Core Testing
 * License: GPL
 *
 * First Written:   2013-05-21
 *
 * Description:
 *
 * Tests expansion of macros and escaping.
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
#include "naemon/objects.h"
#include "naemon/macros.h"
#include "naemon/utils.h"
#include "naemon/nm_alloc.h"
#include "tap.h"

#define TEST_HOSTNAME "name'&%"
#define TEST_HOSTGROUPNAME "hostgroup name'&%"

/*****************************************************************************/
/*                             Local test environment                        */
/*****************************************************************************/

static struct host test_host = {
	.name = TEST_HOSTNAME,
	.address = "address'&%",
	.notes_url = "notes_url'&%($HOSTNOTES$)",
	.notes = "notes'&%\"($HOSTACTIONURL$)",
	.action_url = "action_url'&%",
	.plugin_output = "name'&%",
	.check_command = "check_command!3!\"Some output\""
};

static struct service test_service = {
	.description = "service description",
	.notes_url = "notes_url'&%($SERVICENOTES$)",
	.notes = "notes'&%\"($SERVICEACTIONURL$)",
	.action_url = "action_url'&%",
	.plugin_output = "name'&%",
	.check_command = "check_command!3!\"Some output\"",
	.current_state = 2,
};

static struct hostgroup test_hostgroup = {
	.group_name = TEST_HOSTGROUPNAME,
	.notes = "notes'&%\"($HOSTGROUPACTIONURL$)",
	.action_url = "action_url'&%",
};

static struct servicegroup test_servicegroup = {
	.group_name = "servicegroup name'&%",
	.notes = "notes'&%\"($SERVICEGROUPACTIONURL$)",
	.action_url = "action_url'&%",
};

/*****************************************************************************/
/*                             Helper functions                              */
/*****************************************************************************/

void init_environment(void)
{
	char *p;

	nm_free(illegal_output_chars);
	illegal_output_chars = strdup("'&\""); /* For this tests, remove ', " and & */

	/* This is a part of preflight check, which we can't run */
	for (p = illegal_output_chars; *p; p++) {
		illegal_output_char_map[(int) *p] = 1;
	}
	init_objects_host(1);
	init_objects_service(1);
	init_objects_hostgroup(1);
	test_service.host_name = test_host.name;
	test_service.host_ptr = &test_host;
	register_host(&test_host);
	register_service(&test_service);
	test_hostgroup.members = g_tree_new_full((GCompareDataFunc)my_strsorter, NULL, g_free, NULL);
	register_hostgroup(&test_hostgroup);
	add_host_to_hostgroup(&test_hostgroup, &test_host);
}

nagios_macros *setup_macro_object(void)
{
	nagios_macros *mac = (nagios_macros *) calloc(1, sizeof(nagios_macros));
	grab_service_macros_r(mac, &test_service);
	grab_hostgroup_macros_r(mac, &test_hostgroup);
	grab_servicegroup_macros_r(mac, &test_servicegroup);
	return mac;
}

#define RUN_MACRO_TEST(_STR, _EXPECT, _OPTS) \
	do { \
		if( OK == process_macros_r(mac, (_STR), &output, _OPTS ) ) {\
			ok( 0 == strcmp( output, _EXPECT ), "'%s': '%s' == '%s'", (_STR), output, (_EXPECT) ); \
		} else { \
			fail( "process_macros_r returns ERROR for " _STR ); \
		} \
	} while(0)

#define RUN_MACRO_TEST_EXPECT_SAME(_STR, _OPTS) \
	do { RUN_MACRO_TEST(_STR, _STR, _OPTS); } while (0)

/*****************************************************************************/
/*                             Tests                                         */
/*****************************************************************************/

void test_escaping(nagios_macros *mac)
{
	char *output;

	/* Nothing should be changed... options == 0 */
	RUN_MACRO_TEST("$HOSTNAME$ '&%", TEST_HOSTNAME " '&%", 0);

	/* Able to escape illegal macro chars in HOSTCHECKCOMMAND */
	RUN_MACRO_TEST("$HOSTCHECKCOMMAND$ '&%", "check_command!3!Some output '&%", STRIP_ILLEGAL_MACRO_CHARS);
	RUN_MACRO_TEST("$HOSTCHECKCOMMAND$ '&%", "check_command!3!\"Some output\" '&%", 0);

	RUN_MACRO_TEST("$HOSTNOTES$", "notes%(action_url%)", STRIP_ILLEGAL_MACRO_CHARS);
	RUN_MACRO_TEST("$HOSTNOTES$", "notes'&%\"(action_url'&%)", 0);

	RUN_MACRO_TEST("$SERVICENOTES$", "notes%(action_url%)", STRIP_ILLEGAL_MACRO_CHARS);
	RUN_MACRO_TEST("$SERVICENOTES$", "notes'&%\"(action_url'&%)", 0);

	RUN_MACRO_TEST("$HOSTGROUPNOTES$", "notes%(action_url%)", STRIP_ILLEGAL_MACRO_CHARS);
	RUN_MACRO_TEST("$HOSTGROUPNOTES$", "notes'&%\"(action_url'&%)", 0);

	RUN_MACRO_TEST("$SERVICEGROUPNOTES$", "notes%(action_url%)", STRIP_ILLEGAL_MACRO_CHARS);
	RUN_MACRO_TEST("$SERVICEGROUPNOTES$", "notes'&%\"(action_url'&%)", 0);

	/* Nothing should be changed... HOSTNAME doesn't accept STRIP_ILLEGAL_MACRO_CHARS */
	RUN_MACRO_TEST("$HOSTNAME$ '&%", TEST_HOSTNAME " '&%", STRIP_ILLEGAL_MACRO_CHARS);

	/* ' and & should be stripped from the macro, according to
	 * init_environment(), but not from the initial string
	 */
	RUN_MACRO_TEST("$HOSTOUTPUT$ '&%", "name% '&%", STRIP_ILLEGAL_MACRO_CHARS);

	/* ESCAPE_MACRO_CHARS doesn't seem to do anything... exist always in pair
	 * with STRIP_ILLEGAL_MACRO_CHARS
	 */
	RUN_MACRO_TEST("$HOSTOUTPUT$ '&%", "name'&% '&%", ESCAPE_MACRO_CHARS);
	RUN_MACRO_TEST("$HOSTOUTPUT$ '&%", "name% '&%",
	               STRIP_ILLEGAL_MACRO_CHARS | ESCAPE_MACRO_CHARS);

	/* $HOSTNAME$ should be url-encoded, but not the tailing chars */
	RUN_MACRO_TEST("$HOSTNAME$ '&%", "name%27%26%25 '&%",
	               URL_ENCODE_MACRO_CHARS);

	/* The notes in the notesurl should be url-encoded, no more encoding should
	 * exist
	 */
	RUN_MACRO_TEST("$HOSTNOTESURL$ '&%",
	               "notes_url'&%(notes%27%26%25%22%28action_url%27%26%25%29) '&%", 0);

	/* '& in the source string shouldn't be removed, because HOSTNOTESURL
	 * doesn't accept STRIP_ILLEGAL_MACRO_CHARS, as in the url. the macros
	 * included in the string should be url-encoded, and therefore not contain &
	 * and '
	 */
	RUN_MACRO_TEST("$HOSTNOTESURL$ '&%",
	               "notes_url'&%(notes%27%26%25%22%28action_url%27%26%25%29) '&%",
	               STRIP_ILLEGAL_MACRO_CHARS);

	/* This should double-encode some chars ($HOSTNOTESURL$ should contain
	 * url-encoded chars, and should itself be url-encoded
	 */
	RUN_MACRO_TEST("$HOSTNOTESURL$ '&%",
	               "notes_url%27%26%25%28notes%2527%2526%2525%2522%2528action_url%2527%2526%2525%2529%29 '&%",
	               URL_ENCODE_MACRO_CHARS);

	/* Test for escaped $ value($$) */
	RUN_MACRO_TEST("$$ '&%",
	               "$ '&%",
	               URL_ENCODE_MACRO_CHARS);

	/* Testing for invalid macro */
	RUN_MACRO_TEST_EXPECT_SAME("$IDONOTEXIST$ '&%", URL_ENCODE_MACRO_CHARS);

	/* Testing for incomplete macro */
	RUN_MACRO_TEST_EXPECT_SAME("we have an $ alone", URL_ENCODE_MACRO_CHARS);
}

static void test_ondemand_macros(nagios_macros *mac)
{
	char *output;

	/* first is invalid and shouldn't be substituted */
	RUN_MACRO_TEST_EXPECT_SAME("$SERVICESTATEID:" TEST_HOSTNAME ",service description$", 0);
	/* this is valid and should return the real value as a string */
	RUN_MACRO_TEST("$SERVICESTATEID:" TEST_HOSTNAME ":service description$", "2", 0);

	/* on demand hostgroup macro */
	RUN_MACRO_TEST("$HOSTSTATEID:" TEST_HOSTGROUPNAME ":,$", "0", 0);
	RUN_MACRO_TEST("$HOSTNAME:" TEST_HOSTGROUPNAME ":,$", TEST_HOSTNAME, 0);
}

/*****************************************************************************/
/*                             Main function                                 */
/*****************************************************************************/

int main(void)
{
	nagios_macros *mac;

	plan_tests(26);

	reset_variables();
	init_environment();
	init_macros();

	mac = setup_macro_object();

	test_escaping(mac);
	test_ondemand_macros(mac);

	free(mac);

	return exit_status();
}
