#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "config.h"
#include "common.h"
#include "objects.h"
#include "comments.h"
#include "downtime.h"
#include "statusdata.h"
#include "macros.h"
#include "sretention.h"
#include "perfdata.h"
#include "broker.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "workers.h"
#include "nerd.h"
#include "query-handler.h"
#include "configuration.h"
#include "commands.h"
#include "events.h"
#include "utils.h"
#include "defaults.h"
#include "loadctl.h"
#include "globals.h"
#include "logging.h"
#include <dlfcn.h>
#include <getopt.h>
#include <glob.h>
#include <stdarg.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

struct result_l {
    char **set;
    struct result_l * next;
};
typedef struct result_l result_list;

#define LIVESTATUS_MODE_SOCKET 1
#define LIVESTATUS_MODE_TCP    2
#define LIVESTATUS_MODE_HTTP   3

// from livestatus global_counters.h
#define COUNTER_NEB_CALLBACKS        0
#define COUNTER_REQUESTS             1
#define COUNTER_CONNECTIONS          2
#define COUNTER_SERVICE_CHECKS       3
#define COUNTER_HOST_CHECKS          4
#define COUNTER_FORKS                5
#define COUNTER_LOG_MESSAGES         6
#define COUNTER_COMMANDS             7
#define COUNTER_LIVECHECKS           8
#define COUNTER_LIVECHECK_OVERFLOWS  9
#define NUM_COUNTERS                10

int main(int argc, char **argv);
void usage(const char *fmt, ...);
int initialize_core(void);
int deinitialize_core(void);
int livestatus_query(result_list **result, char *input_source, char *query, char *columns[], int columnssize);
int livestatus_query_socket(result_list **result, char *socket_path, char *query, char *columns[], int columnssize);
void free_livestatus_result(result_list * result, int datasize);
int update_all_runtime_data(void);
int update_program_status_data(void);
int update_host_status_data(char *);
int update_service_status_data(char *, char *);
int update_external_commands(void);
int update_downtime_data(void);
int update_downtime_data_by_id(unsigned long);
int update_comment_data(void);
int update_comment_data_by_id(unsigned long);
char *get_default_livestatus_module(void);
int clean_output_folder(void);
int write_config_files(void);
int write_commands_configuration(FILE *file);
int write_timeperiods_configuration(FILE *file);
int write_contactgroups_configuration(FILE *file);
int write_hostgroups_configuration(FILE *file);
int write_servicegroups_configuration(FILE *file);
int write_contacts_configuration(FILE *file);
int write_hosts_configuration(FILE *file);
int write_services_configuration(FILE *file);
int run_refresh_loop(void);
int open_local_socket(char *socket_path);
int open_tcp_socket(char *connection_string);
int write_list_attribute(FILE *file, char* attr, char* rawlist);
int write_custom_variables(FILE *file, char* rawnames, char* rawvalues);
int get_delta_request_count(void);
