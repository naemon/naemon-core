#ifndef _UTILS_H
#define _UTILS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <string.h>
#include "checks.h"
#include "objects_timeperiod.h"
#include "objects_command.h"
#include "macros.h"
#include "lib/lnae-utils.h"

NAGIOS_BEGIN_DECL

#define CHECK_STATS_BUCKETS                  15
#define PIPE_READ                             0
#define PIPE_WRITE                            1

/* used for tracking host and service check statistics */
typedef struct check_stats {
	int current_bucket;
	int bucket[CHECK_STATS_BUCKETS];
	int overflow_bucket;
	int minute_stats[3];
	time_t last_update;
} check_stats;

extern struct check_stats check_statistics[MAX_CHECK_STATS_TYPES];

const char *check_result_source(check_result *cr);

void setup_sighandler(void);                         		/* trap signals */
void reset_sighandler(void);                         		/* reset signals to default action */
void signal_react(void);				/* General signal reaction routines */
void handle_sigxfsz(void);				/* handle SIGXFSZ */
int signal_parent(int);					/* signal parent when daemonizing */
int daemon_init(void);				     		/* switches to daemon mode */

int init_check_stats(void);
int update_check_stats(int, time_t);
int generate_check_stats(void);

void free_memory(nagios_macros *mac);                              	/* free memory allocated to all linked lists in memory */
int reset_variables(void);                           	/* reset all global variables */

void sighandler(int);                                	/* handles signals */
int my_rename(char *, char *);                          /* renames a file */

time_t get_next_log_rotation_time(void);	     	/* determine the next time to schedule a log rotation */
int set_environment_var(char *, char *, int);           /* sets/clears and environment variable */

const char *get_program_version(void);

void cleanup(void);                                  	/* cleanup after ourselves (before quitting or restarting) */

char *escape_plugin_output(const char *);
char *unescape_plugin_output(const char *);

NAGIOS_END_DECL

#endif
