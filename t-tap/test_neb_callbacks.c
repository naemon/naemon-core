#include "fixtures.h"

#include "naemon/nebmods.h"
#include "naemon/broker.h"
#include "naemon/nebstructs.h"
#include "naemon/statusdata.h"
#include "naemon/globals.h"
#include "naemon/checks.h"
#include "tap.h"
#include <assert.h>
#define NUM_NEBTYPES 2000
nebmodule *test_nebmodule;
void *received_callback_data[NEBCALLBACK_NUMITEMS][NUM_NEBTYPES];

void clear_callback_data(void) {
	int i = 0, j = 0;
	for (i = 0; i < NEBCALLBACK_NUMITEMS; i++) {
		for(j = 0; j < NUM_NEBTYPES; j++) {
			if (received_callback_data[i][j] != NULL) {
				free(received_callback_data[i][j]);
			}
			received_callback_data[i][j] = NULL;
		}
	}

}
int _test_cb(int type, void *data)
{
	/*
	 * the first member of all nebstruct_'s is an int denoting the type,
	 * this hackery allows us to avoid a huge switch-case
	 * */
	size_t sz = 0;
	int nebtype = (int) *((int *)data);
	switch(nebtype) {
		case NEBTYPE_SERVICECHECK_PROCESSED:
			sz = sizeof(nebstruct_service_check_data);
			break;
		case NEBTYPE_HOSTCHECK_PROCESSED:
			sz = sizeof(nebstruct_host_check_data);
			break;
		default:
			assert(!"Unhandled nebtype - update _test_cb()");
	}
	received_callback_data[type][nebtype] = malloc(sz);
	memcpy( received_callback_data[type][nebtype], data, sz);
	return 0;
}

int test_cb_service_check_processed(void)
{
	struct check_result *cr = check_result_new(0, "Some output");
	struct host *hst = host_new("MyHost");
	struct service *svc = service_new(hst, "MyService");
	nebstruct_service_check_data *ds = NULL;

	assert(OK == neb_register_callback(NEBCALLBACK_SERVICE_CHECK_DATA, test_nebmodule->module_handle, 0,  _test_cb));
	event_broker_options = BROKER_EVERYTHING;
	assert(OK == handle_async_service_check_result(svc, cr));
	ds = (nebstruct_service_check_data *) received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ok(ds != NULL, "SERVICE_CHECK_DATA callback invoked");
	ok(ds->type == NEBTYPE_SERVICECHECK_PROCESSED, "nebstruct has expected type") || diag("Type was %d", ds->type);
	ok(!strcmp(ds->host_name, "MyHost"), "nebstruct has expected hostname");
	ok(!strcmp(ds->service_description, "MyService"), "nebstruct has expected service description");
	ok(ds->attr == NEBATTR_NONE, "nebstruct has no attributes set");
	clear_callback_data();

	/* test stalking */
	/* a change in plugin output should result in the NEBATTR_CHECK_ALERT attribute being set
	 * for a service, for which stalking is enabled
	 *
	 * This output change is emulated implicitly by our fixture where svc->plugin_output
	 * is "Initial state" and cr->output is "Some output"
	 * */
	service_destroy(svc);
	svc = service_new(hst, "MyService");
	svc->stalking_options |= ~0; /*stalk all the states*/
	assert(OK == handle_async_service_check_result(svc, cr));
	ds = (nebstruct_service_check_data *) received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, output changed)");

	svc->plugin_output = strdup("Initial state");
	svc->long_plugin_output = strdup("Some long output");
	assert(OK == handle_async_service_check_result(svc, cr));
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output appeared)");
	clear_callback_data();

	svc->plugin_output = strdup("Initial state");
	svc->long_plugin_output = strdup("Some other long output");
	assert(OK == handle_async_service_check_result(svc, cr));
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output changed)");
	clear_callback_data();

	assert(OK == handle_async_service_check_result(svc, cr));
	ds = (nebstruct_service_check_data *) received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ok(ds->attr != NEBATTR_CHECK_ALERT, "nebstruct DOES NOT have NEBATTR_CHECK_ALERT attribute set (stalking, but no output changed)");
	clear_callback_data();


	check_result_destroy(cr);
	service_destroy(svc);
	host_destroy(hst);
	return 0;
}

int test_cb_host_check_processed(void)
{
	check_result *cr = check_result_new(0, "Some output");
	struct host *hst = host_new("MyHost");
	nebstruct_host_check_data *ds = NULL;

	assert(OK == neb_register_callback(NEBCALLBACK_HOST_CHECK_DATA, test_nebmodule->module_handle, 0,  _test_cb));
	event_broker_options = BROKER_EVERYTHING;
	assert(OK == handle_async_host_check_result(hst, cr));
	ds = (nebstruct_host_check_data *) received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ok(ds != NULL, "HOST_CHECK_DATA callback invoked");
	ok(ds->type == NEBTYPE_HOSTCHECK_PROCESSED, "nebstruct has expected type") || diag("Type was %d", ds->type);
	ok(!strcmp(ds->host_name, "MyHost"), "nebstruct has expected hostname");
	ok(ds->attr == NEBATTR_NONE, "nebstruct has no attributes set");
	clear_callback_data();

	/* test stalking */
	/*
	 * A change in plugin output should result in the NEBATTR_CHECK_ALERT
	 * attribute being set for a host, for which stalking is enabled
	 */
	hst->plugin_output = strdup("Initial state");
	hst->stalking_options |= ~0; /*stalk all the states*/
	assert(OK == handle_async_host_check_result(hst, cr));
	ds = (nebstruct_host_check_data *) received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, output changed)");
	clear_callback_data();

	hst->plugin_output = strdup("Initial state");
	hst->long_plugin_output = strdup("Some long output");
	assert(OK == handle_async_host_check_result(hst, cr));
	ds = (nebstruct_host_check_data *) received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output appeared)");
	clear_callback_data();

	hst->plugin_output = strdup("Initial state");
	hst->long_plugin_output = strdup("Some other long output");
	assert(OK == handle_async_host_check_result(hst, cr));
	ds = (nebstruct_host_check_data *) received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct has NEBATTR_CHECK_ALERT attribute set (stalking, long output changed)");
	clear_callback_data();

	assert(OK == handle_async_host_check_result(hst, cr));
	ds = (nebstruct_host_check_data *) received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ok(ds->attr != NEBATTR_CHECK_ALERT, "nebstruct DOES NOT have NEBATTR_CHECK_ALERT attribute set (stalking, but no output changed)");
	clear_callback_data();

	check_result_destroy(cr);
	host_destroy(hst);
	return 0;
}

int main(int argc, char **argv)
{
	plan_tests(17);
	assert(OK == neb_init_callback_list());
	test_nebmodule = malloc(sizeof(nebmodule));
	neb_add_core_module(test_nebmodule);
	test_cb_service_check_processed();
	test_cb_host_check_processed();
	return exit_status();
}
