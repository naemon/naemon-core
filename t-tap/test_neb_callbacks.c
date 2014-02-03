#include "fixtures.h"

#include "naemon/nebmods.h"
#include "naemon/broker.h"
#include "naemon/nebstructs.h"
#include "naemon/statusdata.h"
#include "naemon/globals.h"
#include "tap.h"
#include <assert.h>
#define NUM_NEBTYPES 2000
nebmodule *test_nebmodule;
void *received_callback_data[NEBCALLBACK_NUMITEMS][NUM_NEBTYPES] = {NULL};

void clear_callback_data() {
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
int _test_cb(int type, void* data) {
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

int test_cb_service_check_processed() {
	assert(OK == neb_register_callback(NEBCALLBACK_SERVICE_CHECK_DATA, test_nebmodule->module_handle, 0,  _test_cb));
	event_broker_options = BROKER_EVERYTHING;
	check_result *cr = check_result_new(0, "Some output");
	host *host = host_new("MyHost");
	service *service = service_new(host, "MyService");
	nebstruct_service_check_data *ds = NULL;
	assert(OK == handle_async_service_check_result(service, cr));
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
	 * This output change is emulated implicitly by our fixture where service->plugin_output
	 * is "Initial state" and cr->output is "Some output"
	 * */
	service_destroy(service);
	service = service_new(host, "MyService");
	service->stalking_options |= ~0; /*stalk all the states*/
	assert(OK == handle_async_service_check_result(service, cr));
	ds = (nebstruct_service_check_data *) received_callback_data[NEBCALLBACK_SERVICE_CHECK_DATA][NEBTYPE_SERVICECHECK_PROCESSED];
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct should have NEBATTR_CHECK_ALERT attribute set");

	clear_callback_data();
	check_result_destroy(cr);
	service_destroy(service);
	host_destroy(host);
	return 0;
}

int test_cb_host_check_processed() {
	assert(OK == neb_register_callback(NEBCALLBACK_HOST_CHECK_DATA, test_nebmodule->module_handle, 0,  _test_cb));
	event_broker_options = BROKER_EVERYTHING;
	check_result *cr = check_result_new(0, "Some output");
	host *host = host_new("MyHost");
	nebstruct_host_check_data *ds = NULL;
	assert(OK == handle_async_host_check_result(host, cr));
	ds = (nebstruct_host_check_data *) received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ok(ds != NULL, "HOST_CHECK_DATA callback invoked");
	ok(ds->type == NEBTYPE_HOSTCHECK_PROCESSED, "nebstruct has expected type") || diag("Type was %d", ds->type);
	ok(!strcmp(ds->host_name, "MyHost"), "nebstruct has expected hostname");
	ok(ds->attr == NEBATTR_NONE, "nebstruct has no attributes set");
	clear_callback_data();

	/* test stalking */
	/* a change in plugin output should result in the NEBATTR_CHECK_ALERT attribute being set
	 * for a host, for which stalking is enabled
	 * */
	host->plugin_output = strdup("Initial state");
	host->stalking_options |= ~0; /*stalk all the states*/
	assert(OK == handle_async_host_check_result(host, cr));
	ds = (nebstruct_host_check_data *) received_callback_data[NEBCALLBACK_HOST_CHECK_DATA][NEBTYPE_HOSTCHECK_PROCESSED];
	ok(ds->attr == NEBATTR_CHECK_ALERT, "nebstruct has NEBATTR_CHECK_ALERT attribute set");

	clear_callback_data();
	check_result_destroy(cr);
	host_destroy(host);
	return 0;
}
int main(int argc, char **argv) {
	plan_tests(11);
	assert(OK == neb_init_callback_list());
	test_nebmodule = malloc(sizeof(nebmodule));
	neb_add_core_module(test_nebmodule);
	test_cb_service_check_processed();
	test_cb_host_check_processed();
	return exit_status();
}
