#ifndef INCLUDE_objects_timeperiod_h__
#define INCLUDE_objects_timeperiod_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"

NAGIOS_BEGIN_DECL

#define DATERANGE_CALENDAR_DATE  0  /* 2008-12-25 */
#define DATERANGE_MONTH_DATE     1  /* july 4 (specific month) */
#define DATERANGE_MONTH_DAY      2  /* day 21 (generic month) */
#define DATERANGE_MONTH_WEEK_DAY 3  /* 3rd thursday (specific month) */
#define DATERANGE_WEEK_DAY       4  /* 3rd thursday (generic month) */
#define DATERANGE_TYPES          5

struct timeperiod;
typedef struct timeperiod timeperiod;
struct timerange;
typedef struct timerange timerange;
struct daterange;
typedef struct daterange daterange;
struct timeperiodexclusion;
typedef struct timeperiodexclusion timeperiodexclusion;

extern struct timeperiod *timeperiod_list;
extern struct timeperiod **timeperiod_ary;

struct timeperiod {
	unsigned int id;
	char    *name;
	char    *alias;
	struct timerange *days[7];
	struct daterange *exceptions[DATERANGE_TYPES];
	struct timeperiodexclusion *exclusions;
	struct timeperiod *next;
};

struct timerange {
	unsigned long range_start;
	unsigned long range_end;
	struct timerange *next;
};


struct daterange {
	int type;
	int syear;          /* start year */
	int smon;           /* start month */
	int smday;          /* start day of month (may 3rd, last day in feb) */
	int swday;          /* start day of week (thursday) */
	int swday_offset;   /* start weekday offset (3rd thursday, last monday in jan) */
	int eyear;
	int emon;
	int emday;
	int ewday;
	int ewday_offset;
	int skip_interval;
	struct timerange *times;
	struct daterange *next;
};

struct timeperiodexclusion {
	char  *timeperiod_name;
	struct timeperiod *timeperiod_ptr;
	struct timeperiodexclusion *next;
};

int init_objects_timeperiod(int elems);
void destroy_objects_timeperiod(void);

struct timeperiod *create_timeperiod(const char *, const char *);
int register_timeperiod(timeperiod *new_timeperiod);
void destroy_timeperiod(timeperiod *period);

struct timeperiodexclusion *add_exclusion_to_timeperiod(timeperiod *, char *);
struct timerange *add_timerange_to_timeperiod(timeperiod *, int, unsigned long, unsigned long);
struct daterange *add_exception_to_timeperiod(timeperiod *, int, int, int, int, int, int, int, int, int, int, int, int);
struct timerange *add_timerange_to_daterange(daterange *, unsigned long, unsigned long);

struct timeperiod *find_timeperiod(const char *);

void fcache_timeperiod(FILE *fp, const struct timeperiod *temp_timeperiod);

int check_time_against_period(time_t, const timeperiod *);	/* check to see if a specific time is covered by a time period */
void get_next_valid_time(time_t, time_t *, timeperiod *);	/* get the next valid time in a time period */

NAGIOS_END_DECL
#endif
