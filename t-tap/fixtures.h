#include "naemon/objects.h"
#include <string.h>
void check_result_destroy(check_result *cr);
check_result *check_result_new(int status, const char *output);

void host_destroy(host *host);
host *host_new(const char *name);

void service_destroy(service *service);
service *service_new(host *host, const char *service_description);
