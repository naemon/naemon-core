#ifndef _UTILS_H
#define _UTILS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "objects.h"
#include "macros.h"
#include "lib/lnae-utils.h"

NAGIOS_BEGIN_DECL

const char *check_result_source(check_result *cr);
int set_loadctl_options(char *opts, unsigned int len);

void setup_sighandler(void);                         		/* trap signals */
void reset_sighandler(void);                         		/* reset signals to default action */
void signal_react(void);				/* General signal reaction routines */
void handle_sigxfsz(void);				/* handle SIGXFSZ */
int daemon_init(void);				     		/* switches to daemon mode */
int drop_privileges(char *, char *);				/* drops privileges before startup */

int process_check_result_queue(char *);
int process_check_result_file(char *);
int process_check_result(check_result *);
int delete_check_result_file(char *);
int init_check_result(check_result *);
int free_check_result(check_result *);                  	/* frees memory associated with a host/service check result */
int my_system(char *, int, int *, double *, char **, int);         	/* executes a command via popen(), but also protects against timeouts */
int my_system_r(nagios_macros *mac, char *, int, int *, double *, char **, int); /* thread-safe version of the above */

int init_check_stats(void);
int update_check_stats(int, time_t);
int generate_check_stats(void);

void free_memory(nagios_macros *mac);                              	/* free memory allocated to all linked lists in memory */
int reset_variables(void);                           	/* reset all global variables */
void free_notification_list(void);		     	/* frees all memory allocated to the notification list */

void sighandler(int);                                	/* handles signals */
void my_system_sighandler(int);				/* handles timeouts when executing commands via my_system() */
/* FIXME: unused? */
char *get_next_string_from_buf(char *buf, int *start_index, int bufsize);
int compare_strings(char *, char *);                    /* compares two strings for equality */
/* FIXME: unused? */
char *escape_newlines(char *);
int contains_illegal_object_chars(char *);		/* tests whether or not an object name (host, service, etc.) contains illegal characters */
int my_rename(char *, char *);                          /* renames a file - works across filesystems */
int my_fcopy(char *, char *);                           /* copies a file - works across filesystems */
int my_fdcopy(char *, char *, int);                     /* copies a named source to an already opened destination file */

/* thread-safe version of get_raw_command_line_r() */
int get_raw_command_line_r(nagios_macros *mac, command *, char *, char **, int);

/*
 * given a raw command line, determine the actual command to run
 * Manipulates global_macros.argv and is thus not threadsafe
 */
/* unused? */
int get_raw_command_line(command *, char *, char **, int);

int check_time_against_period(time_t, timeperiod *);	/* check to see if a specific time is covered by a time period */
int is_daterange_single_day(daterange *);
time_t calculate_time_from_weekday_of_month(int, int, int, int);	/* calculates midnight time of specific (3rd, last, etc.) weekday of a particular month */
time_t calculate_time_from_day_of_month(int, int, int);	/* calculates midnight time of specific (1st, last, etc.) day of a particular month */
void get_next_valid_time(time_t, time_t *, timeperiod *);	/* get the next valid time in a time period */
/* to events.c? */
time_t get_next_log_rotation_time(void);	     	/* determine the next time to schedule a log rotation */
int dbuf_init(dbuf *, int);
int dbuf_free(dbuf *);
int dbuf_strcat(dbuf *, const char *);
int set_environment_var(char *, char *, int);           /* sets/clears and environment variable */

const char *get_program_version(void);
const char *get_program_modification_date(void);

void cleanup(void);                                  	/* cleanup after ourselves (before quitting or restarting) */

NAGIOS_END_DECL

#endif
