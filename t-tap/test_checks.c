#include <string.h>
#include "naemon/checks.h"
#include "naemon/checks_host.h"
#include "naemon/checks_service.h"
#include "naemon/configuration.h"
#include "naemon/comments.h"
#include "naemon/common.h"
#include "naemon/events.h"
#include "naemon/statusdata.h"
#include "naemon/downtime.h"
#include "naemon/globals.h"
#include "naemon/macros.h"
#include "naemon/broker.h"
#include "naemon/perfdata.h"
#include "tap.h"

int date_format;

/* Test specific functions + variables */
service *svc1 = NULL, *svc2 = NULL;
host *host1 = NULL;
int found_log_rechecking_host_when_service_wobbles = 0;
int found_log_run_async_host_check = 0;
check_result *tmp_check_result;

void setup_check_result(void)
{
	struct timeval start_time, finish_time;
	start_time.tv_sec = 1234567890L;
	start_time.tv_usec = 0L;
	finish_time.tv_sec = 1234567891L;
	finish_time.tv_usec = 0L;

	tmp_check_result = (check_result *)calloc(1, sizeof(check_result));
	tmp_check_result->check_type = SERVICE_CHECK_ACTIVE;
	tmp_check_result->check_options = 0;
	tmp_check_result->scheduled_check = TRUE;
	tmp_check_result->exited_ok = TRUE;
	tmp_check_result->return_code = 0;
	tmp_check_result->output = strdup("Fake result");
	tmp_check_result->latency = 0.6969;
	tmp_check_result->start_time = start_time;
	tmp_check_result->finish_time = finish_time;
}

void destroy_objects(void)
{
	destroy_objects_host();
	destroy_objects_service();
}

void setup_objects(time_t when)
{
	init_objects_host(1);
	init_objects_service(2);

	enable_predictive_service_dependency_checks = FALSE;

	host1 = create_host("Host1");
	ok(host1 != NULL, "Host creation was successful");
	host1->address = strdup("127.0.0.1");
	host1->max_attempts = 5;
	host1->check_command = strdup("a_command");
	register_host(host1);
	host1->state_type = SOFT_STATE;
	host1->current_state = STATE_DOWN;
	host1->has_been_checked = TRUE;
	host1->last_check = when;
	host1->next_check = when;

	/* First service is a normal one */
	svc1 = create_service(host1, "Normal service");
	ok(svc1 != NULL, "First service creation was successful");
	register_service(svc1);
	svc1->max_attempts = 4;
	svc1->check_interval = 5;
	svc1->retry_interval = 1;
	svc1->check_options = 0;
	svc1->next_check = when;
	svc1->state_type = SOFT_STATE;
	svc1->current_state = STATE_CRITICAL;
	svc1->current_attempt = 1;
	svc1->last_state_change = 0;
	svc1->last_state_change = 0;
	svc1->last_check = (time_t)1234560000;
	svc1->host_problem_at_last_check = FALSE;
	svc1->plugin_output = strdup("Initial state");
	svc1->last_hard_state_change = (time_t)1111111111;

	/* Second service .... to be configured! */
	svc2 = create_service(host1, "To be nudged");
	ok(svc2 != NULL, "First service creation was successful");
	register_service(svc2);
	svc2->max_attempts = 4;
	svc2->check_interval = 5;
	svc2->retry_interval = 1;
	svc2->next_check = when;
	svc2->state_type = SOFT_STATE;
	svc2->current_state = STATE_OK;
}

int main(int argc, char **argv)
{
	time_t now = 0L;


	plan_tests(50);

	init_event_queue();
	time(&now);


	/* Test to confirm that if a service is warning, the notified_on OPT_CRITICAL flag is reset */
	tmp_check_result = (check_result *)calloc(1, sizeof(check_result));
	tmp_check_result->host_name = strdup("host1");
	tmp_check_result->service_description = strdup("Normal service");
	tmp_check_result->object_check_type = SERVICE_CHECK;
	tmp_check_result->check_type = SERVICE_CHECK_ACTIVE;
	tmp_check_result->check_options = 0;
	tmp_check_result->scheduled_check = TRUE;
	tmp_check_result->latency = 0.666;
	tmp_check_result->start_time.tv_sec = 1234567890;
	tmp_check_result->start_time.tv_usec = 56565;
	tmp_check_result->finish_time.tv_sec = 1234567899;
	tmp_check_result->finish_time.tv_usec = 45454;
	tmp_check_result->early_timeout = 0;
	tmp_check_result->exited_ok = TRUE;
	tmp_check_result->return_code = 1;
	tmp_check_result->output = strdup("Warning - check notified_on OPT_CRITICAL flag reset");

	setup_objects(now);
	svc1->last_state = STATE_CRITICAL;
	svc1->notified_on |= OPT_CRITICAL;
	svc1->current_notification_number = 999;
	svc1->last_notification = (time_t)11111;
	svc1->next_notification = (time_t)22222;
	svc1->no_more_notifications = TRUE;

	handle_async_service_check_result(svc1, tmp_check_result);

	ok(svc1->last_notification == (time_t)0, "last notification reset due to state change");
	ok(svc1->next_notification == (time_t)0, "next notification reset due to state change");
	ok(svc1->no_more_notifications == FALSE, "no_more_notifications reset due to state change");
	ok(svc1->current_notification_number == 999, "notification number NOT reset");

	/* Test case:
		service that transitions from OK to CRITICAL (where its host is set to DOWN) will get set to a hard state
		even though check attempts = 1 of 4

		__test case disabled__
		Reason: It should, but it doesn't.

		Since we now (as per a466b8f75d19819ca329069b555ea8c3fb872717) use only asynchronous checks,
		we cannot get the routing result necessary to determine the real state of the host before we're done
		evaluating the service check result. This means that we'll always run max_attempts checks for
		a service before we set it in a hard state.

		The solution proposed to mitigate this issue is to add some kind of trigger to checks,
		such that (in this case) the scheduled host check, when finished would update the state
		type of the service appropriately.

	*/
/**
*	setup_objects((time_t) 1234567800L);
*	host1->current_state = STATE_DOWN;
*	svc1->current_state = STATE_OK;
*	svc1->state_type = HARD_STATE;
*	setup_check_result();
*	tmp_check_result->return_code = STATE_CRITICAL;
*	tmp_check_result->output = strdup("CRITICAL failure");
*
*	handle_async_service_check_result(svc1, tmp_check_result);
*
*	ok(svc1->last_hard_state_change == (time_t)1234567890, "Got last_hard_state_change time=%lu", svc1->last_hard_state_change);
*	ok(svc1->last_state_change == svc1->last_hard_state_change, "Got same last_state_change");
*	ok(svc1->last_hard_state == 2, "Should save the last hard state as critical for next time");
*	ok(svc1->host_problem_at_last_check == TRUE, "Got host_problem_at_last_check set to TRUE due to host failure - this needs to be saved otherwise extra alerts raised in subsequent runs");
*	ok(svc1->state_type == HARD_STATE, "This should be a HARD state since the host is in a failure state");
*	ok(svc1->current_attempt == 1, "Previous status was OK, so this failure should show current_attempt=1") || diag("Current attempt=%d", svc1->current_attempt);
**/
	destroy_objects();




	/* Test case:
		OK -> WARNING 1/4 -> ack -> WARNING 2/4 -> OK transition
		Tests that the ack is left for 2/4
	*/
	setup_objects(now);
	host1->current_state = STATE_UP;
	host1->max_attempts = 4;
	svc1->last_state = STATE_OK;
	svc1->last_hard_state = STATE_OK;
	svc1->current_state = STATE_OK;
	svc1->state_type = SOFT_STATE;

	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure");
	handle_async_service_check_result(svc1, tmp_check_result);

	ok(svc1->last_notification == (time_t)0, "last notification reset due to state change");
	ok(svc1->next_notification == (time_t)0, "next notification reset due to state change");
	ok(svc1->no_more_notifications == FALSE, "no_more_notifications reset due to state change");
	ok(svc1->current_notification_number == 0, "notification number reset");
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "No acks");

	svc1->acknowledgement_type = ACKNOWLEDGEMENT_NORMAL;

	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure");
	handle_async_service_check_result(svc1, tmp_check_result);

	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL, "Ack left");

	setup_check_result();
	tmp_check_result->return_code = STATE_OK;
	tmp_check_result->output = strdup("Back to OK");
	handle_async_service_check_result(svc1, tmp_check_result);

	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "Ack reset to none");
	destroy_objects();




	/* Test case:
	   OK -> WARNING 1/4 -> ack -> WARNING 2/4 -> WARNING 3/4 -> WARNING 4/4 -> WARNING 4/4 -> OK transition
	   Tests that the ack is not removed on hard state change
	*/
	setup_objects(now);
	host1->current_state = STATE_UP;
	host1->max_attempts = 4;
	svc1->last_state = STATE_OK;
	svc1->last_hard_state = STATE_OK;
	svc1->current_state = STATE_OK;
	svc1->state_type = SOFT_STATE;
	svc1->current_attempt = 1;

	setup_check_result();
	tmp_check_result->return_code = STATE_OK;
	tmp_check_result->output = strdup("Reset to OK");
	handle_async_service_check_result(svc1, tmp_check_result);

	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure 1");
	handle_async_service_check_result(svc1, tmp_check_result);

	ok(svc1->state_type == SOFT_STATE, "Soft state");
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "No acks - testing transition to hard warning state");

	svc1->acknowledgement_type = ACKNOWLEDGEMENT_NORMAL;

	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure 2");
	handle_async_service_check_result(svc1, tmp_check_result);
	ok(svc1->state_type == SOFT_STATE, "Soft state");
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL, "Ack left");

	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure 3");
	handle_async_service_check_result(svc1, tmp_check_result);
	ok(svc1->state_type == SOFT_STATE, "Soft state");
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL, "Ack left");

	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure 4");
	handle_async_service_check_result(svc1, tmp_check_result);
	ok(svc1->state_type == HARD_STATE, "Hard state");
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL, "Ack left on hard failure");

	setup_check_result();
	tmp_check_result->return_code = STATE_OK;
	tmp_check_result->output = strdup("Back to OK");
	handle_async_service_check_result(svc1, tmp_check_result);
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "Ack removed");
	destroy_objects();




	/* Test case:
	   OK -> WARNING 1/1 -> ack -> WARNING -> OK transition
	   Tests that the ack is not removed on 2nd warning, but is on OK
	*/
	setup_objects(now);
	host1->current_state = STATE_UP;
	host1->max_attempts = 4;
	svc1->last_state = STATE_OK;
	svc1->last_hard_state = STATE_OK;
	svc1->current_state = STATE_OK;
	svc1->state_type = SOFT_STATE;
	svc1->current_attempt = 1;
	svc1->max_attempts = 2;
	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure 1");

	handle_async_service_check_result(svc1, tmp_check_result);

	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "No acks - testing transition to immediate hard then OK");

	svc1->acknowledgement_type = ACKNOWLEDGEMENT_NORMAL;

	setup_check_result();
	tmp_check_result->return_code = STATE_WARNING;
	tmp_check_result->output = strdup("WARNING failure 2");
	handle_async_service_check_result(svc1, tmp_check_result);
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL, "Ack left");

	setup_check_result();
	tmp_check_result->return_code = STATE_OK;
	tmp_check_result->output = strdup("Back to OK");
	handle_async_service_check_result(svc1, tmp_check_result);
	ok(svc1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "Ack removed");
	destroy_objects();


	/* Test case:
	   UP -> DOWN 1/4 -> ack -> DOWN 2/4 -> DOWN 3/4 -> DOWN 4/4 -> UP transition
	   Tests that the ack is not removed on 2nd DOWN, but is on UP
	*/
	setup_objects(now);
	host1->current_state = STATE_UP;
	host1->last_state = STATE_UP;
	host1->last_hard_state = STATE_UP;
	host1->state_type = SOFT_STATE;
	host1->current_attempt = 1;
	host1->max_attempts = 4;
	host1->acknowledgement_type = ACKNOWLEDGEMENT_NONE;
	host1->plugin_output = strdup("");
	host1->long_plugin_output = strdup("");
	host1->perf_data = strdup("");
	host1->check_command = strdup("Dummy command required");
	host1->accept_passive_checks = TRUE;
	passive_host_checks_are_soft = TRUE;
	setup_check_result();

	tmp_check_result->return_code = STATE_CRITICAL;
	tmp_check_result->output = strdup("DOWN failure 2");
	tmp_check_result->check_type = HOST_CHECK_PASSIVE;
	handle_async_host_check_result(host1, tmp_check_result);
	ok(host1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "No ack set");
	if (!ok(host1->current_attempt == 2, "Attempts right (not sure why this goes into 2 and not 1)"))
		diag("current_attempt=%d", host1->current_attempt);
	if (!ok(strcmp(host1->plugin_output, "DOWN failure 2") == 0, "output set"))
		diag("plugin_output=%s", host1->plugin_output);

	host1->acknowledgement_type = ACKNOWLEDGEMENT_NORMAL;

	tmp_check_result->output = strdup("DOWN failure 3");
	handle_async_host_check_result(host1, tmp_check_result);
	ok(host1->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL, "Ack should be retained as in soft state");
	if (!ok(host1->current_attempt == 3, "Attempts incremented"))
		diag("current_attempt=%d", host1->current_attempt);
	if (!ok(strcmp(host1->plugin_output, "DOWN failure 3") == 0, "output set"))
		diag("plugin_output=%s", host1->plugin_output);


	tmp_check_result->output = strdup("DOWN failure 4");
	handle_async_host_check_result(host1, tmp_check_result);
	ok(host1->acknowledgement_type == ACKNOWLEDGEMENT_NORMAL, "Ack should be retained as in soft state");
	if (!ok(host1->current_attempt == 4, "Attempts incremented"))
		diag("current_attempt=%d", host1->current_attempt);
	if (!ok(strcmp(host1->plugin_output, "DOWN failure 4") == 0, "output set"))
		diag("plugin_output=%s", host1->plugin_output);


	tmp_check_result->return_code = STATE_OK;
	tmp_check_result->output = strdup("UP again");
	handle_async_host_check_result(host1, tmp_check_result);
	ok(host1->acknowledgement_type == ACKNOWLEDGEMENT_NONE, "Ack reset due to state change");
	if (!ok(host1->current_attempt == 1, "Attempts reset"))
		diag("current_attempt=%d", host1->current_attempt);
	if (!ok(strcmp(host1->plugin_output, "UP again") == 0, "output set"))
		diag("plugin_output=%s", host1->plugin_output);
	destroy_objects();

	destroy_event_queue();

	return exit_status();
}
