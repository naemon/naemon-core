#ifndef _DOWNTIME_H
#define _DOWNTIME_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "common.h"
#include "objects.h"

NAGIOS_BEGIN_DECL

/* SCHEDULED_DOWNTIME_ENTRY structure */
typedef struct scheduled_downtime {
	int type;
	char *host_name;
	char *service_description;
	time_t entry_time;
	time_t start_time;
	time_t flex_downtime_start;		/* Time the flexible downtime started */
	time_t end_time;
	int fixed;
	unsigned long triggered_by;
	unsigned long duration;
	unsigned long downtime_id;
	int is_in_effect;
	int	start_notification_sent;
	char *author;
	char *comment;
	unsigned long comment_id;
	int start_flex_downtime;
	int incremented_pending_downtime;
	struct scheduled_downtime *next;
	struct timed_event *start_event, *stop_event;
	struct scheduled_downtime *prev;
} scheduled_downtime;

extern struct scheduled_downtime *scheduled_downtime_list;


int initialize_downtime_data(void);        /* initializes scheduled downtime data */
int cleanup_downtime_data(void);           /* cleans up scheduled downtime data */

int add_new_downtime(int, char *, char *, time_t, char *, char *, time_t, time_t, int, unsigned long, unsigned long, unsigned long *, int, int);
int add_new_host_downtime(char *, time_t, char *, char *, time_t, time_t, int, unsigned long, unsigned long, unsigned long *, int, int);
int add_new_service_downtime(char *, char *, time_t, char *, char *, time_t, time_t, int, unsigned long, unsigned long, unsigned long *, int, int);

int delete_host_downtime(unsigned long);
int delete_service_downtime(unsigned long);
int delete_downtime(int, unsigned long);

int schedule_downtime(int, char *, char *, time_t, char *, char *, time_t, time_t, int, unsigned long, unsigned long, unsigned long *);
int unschedule_downtime(int, unsigned long);

int register_downtime(int, unsigned long);
int handle_scheduled_downtime(struct scheduled_downtime *);
int handle_scheduled_downtime_by_id(unsigned long);

int check_pending_flex_host_downtime(struct host *);
int check_pending_flex_service_downtime(struct service *);

int check_for_expired_downtime(void);

int add_host_downtime(char *, time_t, char *, char *, time_t, time_t, time_t, int, unsigned long, unsigned long, unsigned long, int, int);
int add_service_downtime(char *, char *, time_t, char *, char *, time_t, time_t, time_t, int, unsigned long, unsigned long, unsigned long, int, int);

/* If you are going to be adding a lot of downtime in sequence, set
   defer_downtime_sorting to 1 before you start and then call
   sort_downtime afterwards. Things will go MUCH faster. */

extern int defer_downtime_sorting;
int add_downtime(int, char *, char *, time_t, char *, char *, time_t, time_t, time_t, int, unsigned long, unsigned long, unsigned long, int, int);
int sort_downtime(void);

struct scheduled_downtime *find_downtime(int, unsigned long);
struct scheduled_downtime *find_host_downtime(unsigned long);
struct scheduled_downtime *find_service_downtime(unsigned long);

void free_downtime_data(void);                                       /* frees memory allocated to scheduled downtime list */

int delete_downtime_by_hostname_service_description_start_time_comment(char *, char *, time_t, char *);

NAGIOS_END_DECL
#endif
