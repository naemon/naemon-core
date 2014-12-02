#ifndef LOADCTL_H_
#define LOADCTL_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

/* options for load control */
#define LOADCTL_ENABLED    (1 << 0)

NAGIOS_BEGIN_DECL

/*
 * Everything we need to keep system load in check.
 * Don't use this from modules.
 */
struct load_control {
	time_t last_check;  /* last time we checked the real load */
	time_t last_change; /* last time we changed settings */
	time_t check_interval; /* seconds between load checks */
	double load[3];      /* system load, as reported by getloadavg() */
	float backoff_limit; /* limit we must reach before we back off */
	float rampup_limit;  /* limit we must reach before we ramp back up */
	unsigned int backoff_change; /* backoff by this much */
	unsigned int rampup_change;  /* ramp up by this much */
	unsigned int changes;  /* number of times we've changed settings */
	unsigned int jobs_max;   /* upper setting for jobs_limit */
	unsigned int jobs_limit; /* current limit */
	unsigned int jobs_min;   /* lower setting for jobs_limit */
	unsigned int jobs_running;  /* jobs currently running */
	unsigned int nproc_limit;  /* rlimit for user processes */
	unsigned int nofile_limit; /* rlimit for open files */
	unsigned int options; /* various option flags */
};
extern struct load_control loadctl;

NAGIOS_END_DECL


#endif /* LOADCTL_H_ */
