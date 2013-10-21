#include "fixtures.h"

void check_result_destroy(check_result *cr) {
	free(cr->output);
	free(cr);
}

check_result *check_result_new(int status, const char *output) {
	struct timeval start_time, finish_time;
	start_time.tv_sec = (time_t)(time(NULL) - 2);
	start_time.tv_usec = 0L;
	finish_time.tv_sec = (time_t)(time(NULL) - 1);
	finish_time.tv_usec = 0L;

	check_result *cr = calloc(1, sizeof(check_result));
	cr->check_type = SERVICE_CHECK_ACTIVE;
	cr->check_options = 0;
	cr->scheduled_check = TRUE;
	cr->reschedule_check = TRUE;
	cr->exited_ok = TRUE;
	cr->return_code = status;
	cr->output = strdup(output);
	cr->latency = 0.001;
	cr->start_time = start_time;
	cr->finish_time = finish_time;
	cr->early_timeout = 0;
	return cr;
}

void host_destroy(host *host) {
	free(host->name);
	free(host->address);
	free(host);
}

host *host_new(const char *name) {

	host *new_host = (host *)calloc(1, sizeof(host));
	new_host->name = strdup(name);
	new_host->address = strdup("127.0.0.1");
	new_host->retry_interval = 1;
	new_host->check_interval = 5;
	new_host->check_options = 0;
	new_host->state_type = HARD_STATE;
	new_host->current_state = HOST_UP;
	new_host->has_been_checked = TRUE;
	new_host->last_check = (time_t)(time(NULL) - 60);
	new_host->next_check = (time_t)(time(NULL) + 1);
	return new_host;
}

void service_destroy(service *service) {
	free(service->host_name);
	free(service->description);
	free(service->plugin_output);
	free(service);
}

service *service_new(host *host, const char *service_description) {
	service *new_svc = (service *)calloc(1, sizeof(service));
	new_svc->host_name = strdup(host->name);
	new_svc->host_ptr = host;
	new_svc->description = strdup(service_description);
	new_svc->check_options = 0;
	new_svc->next_check = (time_t) (time(NULL) + 1);
	new_svc->state_type = HARD_STATE;
	new_svc->current_state = STATE_OK;
	new_svc->retry_interval = 1;
	new_svc->check_interval = 5;
	new_svc->current_attempt = 1;
	new_svc->max_attempts = 3;
	new_svc->last_state_change = 0;
	new_svc->last_state_change = 0;
	new_svc->last_check = (time_t) (time(NULL) - 60);
	new_svc->host_problem_at_last_check = FALSE;
	new_svc->plugin_output = strdup("Initial state");
	new_svc->last_hard_state_change = 0L;
	return new_svc;
}
