#include <check.h>
#include "naemon/nm_alloc.h"
#include "naemon/events.h"
#include "naemon/nebmods.h"
#include "naemon/broker.h"
#include "naemon/nebstructs.h"
#include "naemon/statusdata.h"
#include "naemon/globals.h"
#include "naemon/checks.h"
#include "naemon/checks_service.h"
#include "naemon/checks_host.h"
#define NUM_NEBTYPES 2000
nebmodule *test_nebmodule;
static void *received_callback_data[NEBCALLBACK_NUMITEMS][NUM_NEBTYPES];

void clear_callback_data(void)
{
	int i = 0, j = 0;
	for (i = 0; i < NEBCALLBACK_NUMITEMS; i++) {
		for (j = 0; j < NUM_NEBTYPES; j++) {
			if (received_callback_data[i][j] != NULL) {
				nm_free(received_callback_data[i][j]);
			}
			received_callback_data[i][j] = NULL;
		}
	}

}

int _test_cb(int type, void *data)
{
	size_t sz = 0;
	/*
	 * the first member of all nebstruct_'s is an int denoting the type,
	 * this hackery allows us to avoid a huge switch-case
	 */
	int nebtype = (int) * ((int *)data);
	switch (nebtype) {
	case NEBTYPE_SERVICECHECK_PROCESSED:
		sz = sizeof(nebstruct_service_check_data);
		break;
	case NEBTYPE_HOSTCHECK_PROCESSED:
		sz = sizeof(nebstruct_host_check_data);
		break;
	default:
		ck_abort_msg("Unhandled nebtype - update _test_cb()");
	}

	ck_assert_msg(received_callback_data[type][nebtype] == NULL, "Cowardly refusing to overwrite existing callback data");
	received_callback_data[type][nebtype] = nm_malloc(sz);
	memcpy(received_callback_data[type][nebtype], data, sz);
	return 0;
}

neb_cb_result *_test_cb_v2(enum NEBCallbackType type, void *data)
{
	neb_cb_result *result = neb_cb_result_create_full(0, data);
	return result;
}

void common_setup(void)
{
	int ret = OK;
	ret = neb_init_callback_list();
	ck_assert_int_eq(OK, ret);
	test_nebmodule = nm_malloc(sizeof(*test_nebmodule));

	ret = neb_add_core_module(test_nebmodule);
	ck_assert_int_eq(0, ret);
}

void common_teardown(void)
{
	nm_free(test_nebmodule);
	neb_free_callback_list();
}

struct check_result *cr;
struct host *hst;
struct service *svc;
void setup_v1(void)
{
	int ret = OK;
	common_setup();
	init_event_queue();
	cr = nm_calloc(1, sizeof(*cr));
	hst = create_host("MyHost");
	svc = create_service(hst, "MyService");
	svc->plugin_output = nm_strdup("Initial state");

	/* We don't want this to be considered the first check, in order
	 * to be able to disregard NEBATTR_CHECK_FIRST */
	svc->last_check = time(NULL) - 60;
	hst->last_check = time(NULL) - 60;

	init_check_result(cr);

	/* For the same reason, we need to set the start time here (and the finish
	 * time for consistency), or the last_check of a checked object will be
	 * reset to zero after being associated with this check result
	 */
	cr->start_time.tv_sec = time(NULL) - 2;
	cr->start_time.tv_usec = 0L;
	cr->finish_time.tv_sec = time(NULL) - 1;
	cr->finish_time.tv_usec = 0L;


	cr->output = nm_strdup("Some output");
	ret = neb_register_callback(NEBCALLBACK_HOST_CHECK_DATA, test_nebmodule->module_handle, 0,  _test_cb);
	ck_assert_int_eq(OK, ret);
	ret = neb_register_callback(NEBCALLBACK_SERVICE_CHECK_DATA, test_nebmodule->module_handle, 0,  _test_cb);
	ck_assert_int_eq(OK, ret);

	event_broker_options = BROKER_EVERYTHING;

}

void setup_v2(void)
{
	int ret = 0;
	common_setup();
	ret = neb_register_callback_full(NEBCALLBACK_PROCESS_DATA,
	                                 test_nebmodule->module_handle, 0, NEB_API_VERSION_2,
	                                 _test_cb_v2);

	ck_assert_int_eq(OK, ret);
	event_broker_options = BROKER_EVERYTHING;
}

void teardown_v1(void)
{
	common_teardown();
	destroy_service(svc, FALSE);
	destroy_host(hst);
	free_check_result(cr);
	nm_free(cr);
	clear_callback_data();
}

void teardown_v2(void)
{
	common_teardown();
}
START_TEST(test_cb_service_check_processed)
{
	int ret = OK;
	nebstruct_service_check_data *ds = NULL;
	ret = handle_async_service_check_result(svc, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ck_assert_msg(ds != NULL, "SERVICE_CHECK_DATA callback invoked");
	ck_assert_msg(ds->type == NEBTYPE_SERVICECHECK_PROCESSED, "nebstruct has expected type (Type was %d)", ds->type);
	ck_assert_str_eq("MyHost", ds->host_name);
	ck_assert_str_eq("MyService", ds->service_description);
	ck_assert_int_eq(ds->attr, NEBATTR_NONE);
}
END_TEST

START_TEST(test_cb_resultset_destroy_null)
{
	/* Just call this with NULL to make sure that nothing segfaults */
	neb_cb_resultset_destroy(NULL);
}
END_TEST

START_TEST(test_cb_service_stalking)
{
	/* test stalking */
	/* a change in plugin output should result in the NEBATTR_CHECK_ALERT attribute being set
	 * for a service, for which stalking is enabled
	 * */
	int ret = OK;
	nebstruct_service_check_data *ds = NULL;
	svc->stalking_options |= ~0; /*stalk all the states*/
	ret = handle_async_service_check_result(svc, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ck_assert_msg(ds != NULL, "SERVICE_CHECK_DATA callback invoked");
	ck_assert_int_eq(NEBATTR_CHECK_ALERT, ds->attr); /*nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, output changed)*/
	clear_callback_data();

	svc->long_plugin_output = nm_strdup("Some long output");
	ret = handle_async_service_check_result(svc, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ck_assert_int_eq(NEBATTR_CHECK_ALERT, ds->attr); /* nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output appeared) */
	clear_callback_data();

	free(svc->long_plugin_output);
	svc->long_plugin_output = nm_strdup("Some other long output");
	ret = handle_async_service_check_result(svc, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ck_assert_int_eq(NEBATTR_CHECK_ALERT, ds->attr); /* nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output changed) */
	clear_callback_data();

	ret = handle_async_service_check_result(svc, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ck_assert_int_ne(NEBATTR_CHECK_ALERT, ds->attr); /* nebstruct DOES NOT have NEBATTR_CHECK_ALERT attribute set (stalking, but no output changed) */

}
END_TEST

START_TEST(test_cb_service_first_check)
{
	int ret = 0;
	nebstruct_service_check_data *ds = NULL;
	/* test first check */
	svc->last_check = 0;
	ret = handle_async_service_check_result(svc, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ck_assert_int_eq(NEBATTR_CHECK_FIRST, ds->attr); /*nebstruct has NEBATTR_CHECK_FIRST attribute set for first check*/
}
END_TEST

START_TEST(test_cb_host_check_processed)
{
	int ret = 0;
	nebstruct_host_check_data *ds = NULL;
	ret = handle_async_host_check_result(hst, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ck_assert_msg(ds != NULL, "HOST_CHECK_DATA callback invoked");
	ck_assert_int_eq(NEBTYPE_HOSTCHECK_PROCESSED, ds->type);
	ck_assert_str_eq("MyHost", ds->host_name);
	ck_assert_int_eq(NEBATTR_NONE, ds->attr);
}
END_TEST

START_TEST(test_cb_host_stalking)
{
	int ret = 0;
	nebstruct_host_check_data *ds = NULL;

	hst->stalking_options |= ~0; /*stalk all the states*/

	ret = handle_async_host_check_result(hst, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ck_assert_msg(ds != NULL, "HOST_CHECK_DATA callback invoked");
	ck_assert_int_eq(NEBATTR_CHECK_ALERT, ds->attr); /*nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, output changed) */
	clear_callback_data();

	hst->long_plugin_output = nm_strdup("Some long output");
	ret = handle_async_host_check_result(hst, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ck_assert_msg(ds != NULL, "HOST_CHECK_DATA callback invoked");
	ck_assert_int_eq(NEBATTR_CHECK_ALERT, ds->attr); /* nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output appeared) */
	clear_callback_data();

	nm_free(hst->long_plugin_output);
	hst->long_plugin_output = strdup("Some other long output");
	ret = handle_async_host_check_result(hst, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ck_assert_msg(ds != NULL, "HOST_CHECK_DATA callback invoked");
	ck_assert_int_eq(NEBATTR_CHECK_ALERT, ds->attr);  /*nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output changed) */
	clear_callback_data();

	ret = handle_async_host_check_result(hst, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ck_assert_msg(ds != NULL, "HOST_CHECK_DATA callback invoked");
	ck_assert_int_ne(NEBATTR_CHECK_ALERT, ds->attr); /*nebstruct DOES NOT have NEBATTR_CHECK_ALERT attribute set (stalking, but no output changed) */
}
END_TEST

START_TEST(test_cb_host_first_check)
{
	int ret = 0;
	nebstruct_host_check_data *ds = NULL;
	hst->last_check = 0;
	ret = handle_async_host_check_result(hst, cr);
	ck_assert_int_eq(OK, ret);
	ds = received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ck_assert_int_eq(NEBATTR_CHECK_FIRST, ds->attr); /* nebstruct has NEBATTR_CHECK_FIRST attribute set for first check */
}
END_TEST

START_TEST(test_cb_api_v2)
{
	neb_cb_resultset *results = NULL;
	neb_cb_resultset_iter iter;
	neb_cb_result *cb_result = NULL;
	void *retp = NULL;
	const char *description = "This is a description for a callback made using NEB callback API version 2";
	results = neb_make_callbacks_full(NEBCALLBACK_PROCESS_DATA, (void *)description);

	neb_cb_resultset_iter_init(&iter, results);

	retp = neb_cb_resultset_iter_next(&iter, &cb_result);
	ck_assert(NULL != retp);
	ck_assert(cb_result != NULL);
	ck_assert_str_eq(description,  neb_cb_result_description(cb_result));
	ck_assert_int_eq(0, neb_cb_result_returncode(cb_result));
	ck_assert_str_eq("Unnamed core module", neb_cb_result_module_name(cb_result));

	retp = neb_cb_resultset_iter_next(&iter, &cb_result);
	ck_assert(NULL == retp);
	ck_assert(NULL == cb_result);
	neb_cb_resultset_destroy(results);
}
END_TEST

Suite *
neb_cb_suite(void)
{
	Suite *s = suite_create("NEB Callbacks");
	TCase *tc_api_version_1 = tcase_create("API Version 1");
	TCase *tc_api_version_2 = tcase_create("API Version 2");
	tcase_add_checked_fixture(tc_api_version_1, setup_v1, teardown_v1);
	tcase_add_test(tc_api_version_1, test_cb_service_check_processed);
	tcase_add_test(tc_api_version_1, test_cb_service_stalking);
	tcase_add_test(tc_api_version_1, test_cb_service_first_check);
	tcase_add_test(tc_api_version_1, test_cb_host_check_processed);
	tcase_add_test(tc_api_version_1, test_cb_host_stalking);
	tcase_add_test(tc_api_version_1, test_cb_host_first_check);
	suite_add_tcase(s, tc_api_version_1);

	tcase_add_checked_fixture(tc_api_version_2, setup_v2, teardown_v2);
	tcase_add_test(tc_api_version_2, test_cb_api_v2);
	tcase_add_test(tc_api_version_2, test_cb_resultset_destroy_null);
	suite_add_tcase(s, tc_api_version_2);
	return s;
}

int main(void)
{
	int number_failed = 0;
	Suite *s = neb_cb_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
