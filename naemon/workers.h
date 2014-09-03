#ifndef INCLUDE_workers_h__
#define INCLUDE_workers_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/libnagios.h"
#include "macros.h"

#define WPROC_FORCE  (1 << 0)

NAGIOS_BEGIN_DECL;

typedef struct wproc_result {
	unsigned int job_id;
	time_t timeout;
	struct timeval start;
	struct timeval stop;
	struct timeval runtime;
	char *command;
	char *outstd;
	char *outerr;
	char *error_msg;
	char *source;
	int wait_status;
	int error_code;
	int exited_ok;
	int early_timeout;
	struct kvvec *response;
	struct rusage rusage;
} wproc_result;

extern unsigned int wproc_num_workers_spawned;
extern unsigned int wproc_num_workers_online;
extern unsigned int wproc_num_workers_desired;

struct load_control; /* TODO: load_control is ugly */

void wproc_reap(int jobs, int msecs);
int wproc_can_spawn(struct load_control *lc);
void free_worker_memory(int flags);
int workers_alive(void);
int init_workers(int desired_workers);

int wproc_run_callback(char *cmt, int timeout, void (*cb)(struct wproc_result *, void *, int), void *data, nagios_macros *mac);

NAGIOS_END_DECL;
#endif
