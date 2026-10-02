#include "objects_timeperiod.h"
#include "nm_alloc.h"
#include "logging.h"
#include "objects_fcache.h"
#include <string.h>
#include <glib.h>

#define SECS_PER_DAY 86400

static int is_daterange_single_day(daterange *);
static time_t calculate_time_from_weekday_of_month(int, int, int, int);	/* calculates midnight time of specific (3rd, last, etc.) weekday of a particular month */
static time_t calculate_time_from_day_of_month(int, int, int);	/* calculates midnight time of specific (1st, last, etc.) day of a particular month */

static GHashTable *timeperiod_hash_table = NULL;
timeperiod **timeperiod_ary = NULL;
timeperiod *timeperiod_list = NULL;

int init_objects_timeperiod(int elems)
{
	timeperiod_ary = nm_calloc(elems, sizeof(timeperiod *));
	timeperiod_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
	return OK;
}

void destroy_objects_timeperiod()
{
	unsigned int i;
	for (i = 0; i < num_objects.timeperiods; i++) {
		timeperiod *this_timeperiod = timeperiod_ary[i];
		destroy_timeperiod(this_timeperiod);
	}
	timeperiod_list = NULL;
	if (timeperiod_hash_table)
		g_hash_table_destroy(timeperiod_hash_table);

	timeperiod_hash_table = NULL;
	nm_free(timeperiod_ary);
	num_objects.timeperiods = 0;
}

timeperiod *create_timeperiod(const char *name, const char *alias)
{
	timeperiod *new_timeperiod = NULL;

	/* make sure we have the data we need */
	if ((name == NULL || !strcmp(name, "")) || (alias == NULL || !strcmp(alias, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Name or alias for timeperiod is NULL\n");
		return NULL;
	}
	if (contains_illegal_object_chars(name) == TRUE) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: The name of time period '%s' contains one or more illegal characters.", name);
		return NULL;
	}

	new_timeperiod = nm_calloc(1, sizeof(*new_timeperiod));

	/* copy string vars */
	new_timeperiod->name = nm_strdup(name);
	new_timeperiod->alias = alias ? nm_strdup(alias) : new_timeperiod->name;

	return new_timeperiod;
}

int register_timeperiod(timeperiod *new_timeperiod)
{

	g_return_val_if_fail(timeperiod_hash_table != NULL, ERROR);

	if ((find_timeperiod(new_timeperiod->name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Timeperiod '%s' has already been defined\n", new_timeperiod->name);
		return ERROR;
	}

	g_hash_table_insert(timeperiod_hash_table, new_timeperiod->name, new_timeperiod);

	new_timeperiod->id = num_objects.timeperiods++;
	if (new_timeperiod->id)
		timeperiod_ary[new_timeperiod->id - 1]->next = new_timeperiod;
	else
		timeperiod_list = new_timeperiod;
	timeperiod_ary[new_timeperiod->id] = new_timeperiod;

	return OK;
}

void destroy_timeperiod(timeperiod *this_timeperiod)
{
	int x;
	timeperiodexclusion *this_timeperiodexclusion, *next_timeperiodexclusion;

	if (!this_timeperiod)
		return;
	/* free the exception time ranges contained in this timeperiod */
	for (x = 0; x < DATERANGE_TYPES; x++) {
		daterange *this_daterange, *next_daterange;
		for (this_daterange = this_timeperiod->exceptions[x]; this_daterange != NULL; this_daterange = next_daterange) {
			timerange *this_timerange, *next_timerange;
			next_daterange = this_daterange->next;
			for (this_timerange = this_daterange->times; this_timerange != NULL; this_timerange = next_timerange) {
				next_timerange = this_timerange->next;
				nm_free(this_timerange);
			}
			nm_free(this_daterange);
		}
	}

	/* free the day time ranges contained in this timeperiod */
	for (x = 0; x < 7; x++) {
		timerange *this_timerange, *next_timerange;
		for (this_timerange = this_timeperiod->days[x]; this_timerange != NULL; this_timerange = next_timerange) {
			next_timerange = this_timerange->next;
			nm_free(this_timerange);
		}
	}

	/* free exclusions */
	for (this_timeperiodexclusion = this_timeperiod->exclusions; this_timeperiodexclusion != NULL; this_timeperiodexclusion = next_timeperiodexclusion) {
		next_timeperiodexclusion = this_timeperiodexclusion->next;
		nm_free(this_timeperiodexclusion->timeperiod_name);
		nm_free(this_timeperiodexclusion);
	}

	if (this_timeperiod->alias != this_timeperiod->name)
		nm_free(this_timeperiod->alias);
	nm_free(this_timeperiod->name);
	nm_free(this_timeperiod);
}


/* adds a new exclusion to a timeperiod */
timeperiodexclusion *add_exclusion_to_timeperiod(timeperiod *period, char *name)
{
	timeperiodexclusion *new_timeperiodexclusion = NULL;
	timeperiod *temp_timeperiod2;

	/* make sure we have enough data */
	if (period == NULL || name == NULL)
		return NULL;

	temp_timeperiod2 = find_timeperiod(name);
	if (temp_timeperiod2 == NULL) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: Excluded time period '%s' specified in timeperiod '%s' is not defined anywhere!", name, period->name);
		return NULL;
	}

	new_timeperiodexclusion = nm_malloc(sizeof(timeperiodexclusion));
	new_timeperiodexclusion->timeperiod_name = nm_strdup(name);
	new_timeperiodexclusion->timeperiod_ptr = temp_timeperiod2;

	new_timeperiodexclusion->next = period->exclusions;
	period->exclusions = new_timeperiodexclusion;

	return new_timeperiodexclusion;
}


/* add a new timerange to a timeperiod */
timerange *add_timerange_to_timeperiod(timeperiod *period, int day, unsigned long start_time, unsigned long end_time)
{
	timerange *prev = NULL, *tr, *new_timerange = NULL;

	/* make sure we have the data we need */
	if (period == NULL)
		return NULL;

	if (day < 0 || day > 6) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Day %d is not valid for timeperiod '%s'\n", day, period->name);
		return NULL;
	}
	if (start_time > 86400) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Start time %lu on day %d is not valid for timeperiod '%s'\n", start_time, day, period->name);
		return NULL;
	}
	if (end_time > 86400) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: End time %lu on day %d is not value for timeperiod '%s'\n", end_time, day, period->name);
		return NULL;
	}

	/* allocate memory for the new time range */
	new_timerange = nm_malloc(sizeof(timerange));
	new_timerange->range_start = start_time;
	new_timerange->range_end = end_time;

	/* insertion-sort the new time range into the list for this day */
	if (!period->days[day] || period->days[day]->range_start > new_timerange->range_start) {
		new_timerange->next = period->days[day];
		period->days[day] = new_timerange;
		return new_timerange;
	}

	for (tr = period->days[day]; tr; tr = tr->next) {
		if (new_timerange->range_start < tr->range_start) {
			new_timerange->next = tr;
			prev->next = new_timerange;
			break;
		}
		if (!tr->next) {
			tr->next = new_timerange;
			new_timerange->next = NULL;
			break;
		}
		prev = tr;
	}

	return new_timerange;
}


/* add a new exception to a timeperiod */
daterange *add_exception_to_timeperiod(timeperiod *period, int type, int syear, int smon, int smday, int swday, int swday_offset, int eyear, int emon, int emday, int ewday, int ewday_offset, int skip_interval)
{
	daterange *new_daterange = NULL;

	/* make sure we have the data we need */
	if (period == NULL)
		return NULL;

	/* allocate memory for the date range */
	new_daterange = nm_malloc(sizeof(daterange));
	new_daterange->times = NULL;
	new_daterange->next = NULL;

	new_daterange->type = type;
	new_daterange->syear = syear;
	new_daterange->smon = smon;
	new_daterange->smday = smday;
	new_daterange->swday = swday;
	new_daterange->swday_offset = swday_offset;
	new_daterange->eyear = eyear;
	new_daterange->emon = emon;
	new_daterange->emday = emday;
	new_daterange->ewday = ewday;
	new_daterange->ewday_offset = ewday_offset;
	new_daterange->skip_interval = skip_interval;

	/* add the new date range to the head of the range list for this exception type */
	new_daterange->next = period->exceptions[type];
	period->exceptions[type] = new_daterange;

	return new_daterange;
}


/* add a new timerange to a daterange */
timerange *add_timerange_to_daterange(daterange *drange, unsigned long start_time, unsigned long end_time)
{
	timerange *new_timerange = NULL;

	/* make sure we have the data we need */
	if (drange == NULL)
		return NULL;

	if (start_time > 86400) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Start time %lu is not valid for timeperiod\n", start_time);
		return NULL;
	}
	if (end_time > 86400) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: End time %lu is not value for timeperiod\n", end_time);
		return NULL;
	}

	/* allocate memory for the new time range */
	new_timerange = nm_malloc(sizeof(timerange));
	new_timerange->range_start = start_time;
	new_timerange->range_end = end_time;

	/* add the new time range to the head of the range list for this date range */
	new_timerange->next = drange->times;
	drange->times = new_timerange;

	return new_timerange;
}

timeperiod *find_timeperiod(const char *name)
{
	return name ? g_hash_table_lookup(timeperiod_hash_table, name) : NULL;
}

static void fc_timerange(struct nm_writebuf *wb, const timerange *tr)
{
	short sh, sm, eh, em;

	sh = tr->range_start / 3600;
	sm = (tr->range_start / 60) % 60;
	eh = tr->range_end / 3600;
	em = (tr->range_end / 60) % 60;
	fc_02d(wb, sh);
	nm_wb_lit(wb, ":");
	fc_02d(wb, sm);
	nm_wb_lit(wb, "-");
	fc_02d(wb, eh);
	nm_wb_lit(wb, ":");
	fc_02d(wb, em);
}

/* " / <n>" for a skip interval, if there is one */
static void fc_skip(struct nm_writebuf *wb, const daterange *dr)
{
	if (dr->skip_interval > 1) {
		nm_wb_lit(wb, " / ");
		nm_wb_int(wb, dr->skip_interval);
	}
}

void nm_fcache_timeperiod(struct nm_writebuf *wb, const timeperiod *temp_timeperiod)
{
	const char *days[7] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
	const char *months[12] = {"january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december"};
	daterange *temp_daterange;
	timerange *tr;
	register int x;

	nm_wb_lit(wb, "define timeperiod {\n");
	fc_str(wb, "timeperiod_name", temp_timeperiod->name);
	if (temp_timeperiod->alias)
		fc_str(wb, "alias", temp_timeperiod->alias);

	if (temp_timeperiod->exclusions) {
		timeperiodexclusion *exclude;
		nm_wb_lit(wb, "\texclude\t");
		for (exclude = temp_timeperiod->exclusions; exclude; exclude = exclude->next) {
			fc_s(wb, exclude->timeperiod_name);
			fc_sep(wb, exclude->next != NULL);
		}
	}

	for (x = 0; x < DATERANGE_TYPES; x++) {
		for (temp_daterange = temp_timeperiod->exceptions[x]; temp_daterange != NULL; temp_daterange = temp_daterange->next) {

			/* skip null entries */
			if (temp_daterange->times == NULL)
				continue;

			switch (temp_daterange->type) {
			case DATERANGE_CALENDAR_DATE:
				nm_wb_lit(wb, "\t");
				nm_wb_int(wb, temp_daterange->syear);
				nm_wb_lit(wb, "-");
				fc_02d(wb, temp_daterange->smon + 1);
				nm_wb_lit(wb, "-");
				fc_02d(wb, temp_daterange->smday);
				if ((temp_daterange->smday != temp_daterange->emday) || (temp_daterange->smon != temp_daterange->emon) || (temp_daterange->syear != temp_daterange->eyear)) {
					nm_wb_lit(wb, " - ");
					nm_wb_int(wb, temp_daterange->eyear);
					nm_wb_lit(wb, "-");
					fc_02d(wb, temp_daterange->emon + 1);
					nm_wb_lit(wb, "-");
					fc_02d(wb, temp_daterange->emday);
				}
				fc_skip(wb, temp_daterange);
				break;
			case DATERANGE_MONTH_DATE:
				nm_wb_lit(wb, "\t");
				nm_wb_str(wb, months[temp_daterange->smon]);
				nm_wb_lit(wb, " ");
				nm_wb_int(wb, temp_daterange->smday);
				if ((temp_daterange->smon != temp_daterange->emon) || (temp_daterange->smday != temp_daterange->emday)) {
					nm_wb_lit(wb, " - ");
					nm_wb_str(wb, months[temp_daterange->emon]);
					nm_wb_lit(wb, " ");
					nm_wb_int(wb, temp_daterange->emday);
					fc_skip(wb, temp_daterange);
				}
				break;
			case DATERANGE_MONTH_DAY:
				nm_wb_lit(wb, "\tday ");
				nm_wb_int(wb, temp_daterange->smday);
				if (temp_daterange->smday != temp_daterange->emday) {
					nm_wb_lit(wb, " - ");
					nm_wb_int(wb, temp_daterange->emday);
					fc_skip(wb, temp_daterange);
				}
				break;
			case DATERANGE_MONTH_WEEK_DAY:
				nm_wb_lit(wb, "\t");
				nm_wb_str(wb, days[temp_daterange->swday]);
				nm_wb_lit(wb, " ");
				nm_wb_int(wb, temp_daterange->swday_offset);
				nm_wb_lit(wb, " ");
				nm_wb_str(wb, months[temp_daterange->smon]);
				if ((temp_daterange->smon != temp_daterange->emon) || (temp_daterange->swday != temp_daterange->ewday) || (temp_daterange->swday_offset != temp_daterange->ewday_offset)) {
					nm_wb_lit(wb, " - ");
					nm_wb_str(wb, days[temp_daterange->ewday]);
					nm_wb_lit(wb, " ");
					nm_wb_int(wb, temp_daterange->ewday_offset);
					nm_wb_lit(wb, " ");
					nm_wb_str(wb, months[temp_daterange->emon]);
					fc_skip(wb, temp_daterange);
				}
				break;
			case DATERANGE_WEEK_DAY:
				nm_wb_lit(wb, "\t");
				nm_wb_str(wb, days[temp_daterange->swday]);
				nm_wb_lit(wb, " ");
				nm_wb_int(wb, temp_daterange->swday_offset);
				if ((temp_daterange->swday != temp_daterange->ewday) || (temp_daterange->swday_offset != temp_daterange->ewday_offset)) {
					nm_wb_lit(wb, " - ");
					nm_wb_str(wb, days[temp_daterange->ewday]);
					nm_wb_lit(wb, " ");
					nm_wb_int(wb, temp_daterange->ewday_offset);
					fc_skip(wb, temp_daterange);
				}
				break;
			default:
				break;
			}

			nm_wb_lit(wb, "\t");
			for (tr = temp_daterange->times; tr; tr = tr->next) {
				fc_timerange(wb, tr);
				fc_sep(wb, tr->next != NULL);
			}
		}
	}
	for (x = 0; x < 7; x++) {
		/* skip null entries */
		if (temp_timeperiod->days[x] == NULL)
			continue;

		nm_wb_lit(wb, "\t");
		nm_wb_str(wb, days[x]);
		nm_wb_lit(wb, "\t");
		for (tr = temp_timeperiod->days[x]; tr; tr = tr->next) {
			fc_timerange(wb, tr);
			fc_sep(wb, tr->next != NULL);
		}
	}
	nm_wb_lit(wb, "\t}\n\n");
}

void fcache_timeperiod(FILE *fp, const timeperiod *temp_timeperiod)
{
	struct nm_writebuf wb;

	fc_file_begin(&wb, fp);
	nm_fcache_timeperiod(&wb, temp_timeperiod);
	nm_writebuf_done(&wb);
}



/******************************************************************/
/************************* TIME FUNCTIONS *************************/
/******************************************************************/

/* Checks if the given time is in daylight time saving period */
static int is_dst_time(time_t *timestamp)
{
	struct tm *bt = localtime(timestamp);
	return bt->tm_isdst;
}


/* Returns the shift in seconds if the given times are across the daylight time saving period change */
static int get_dst_shift(time_t *start, time_t *end)
{
	int shift = 0, dst_end, dst_start;
	dst_start = is_dst_time(start);
	dst_end = is_dst_time(end);
	if (dst_start < dst_end) {
		shift = 3600;
	} else if (dst_start > dst_end) {
		shift = -3600;
	}
	return shift;
}


/*#define TEST_TIMEPERIODS_A 1*/
/*
 * Cache of the local day a timeperiod lookup falls into.
 *
 * Timeperiods are ranges within a day ("09:00-17:00"), so testing a timestamp
 * against one first needs to know when that day started locally. That is what
 * get_midnight() answers: localtime_r() to break the timestamp into local
 * calendar fields, zero the time of day, mktime() back to a time_t. mktime()
 * has to consult the timezone rules and is not cheap, and
 * check_time_against_period() and _get_matching_timerange() were each doing
 * it independently for every dispatched check. Since every caller is asking
 * about "now", the answer is nearly always the same as last time.
 *
 * Why this is not simply "midnight + 86400": a local day is not always 86400
 * seconds long, and its midnight is not always unambiguous. A DST shift makes
 * the day 23 or 25 hours (30 minutes either way on Lord Howe Island), and in
 * zones that transition at midnight -- America/Havana, America/Santiago,
 * Asia/Beirut -- local 00:00 does not exist at all on that day. There the
 * answer genuinely depends on the tm_isdst that localtime_r() reported for
 * the specific timestamp being tested, so such days are never cached:
 * get_day_cache() detects them and falls through to the full computation.
 *
 * The detection has one subtlety, and getting it wrong is the bug this code
 * went through once already. The day length must not be measured starting
 * from the midnight computed for the timestamp under test, because on a
 * transition day that midnight is itself shifted -- a shifted start plus a
 * normal next midnight measures exactly 86400 and makes the day look
 * ordinary. Both ends of the probe must be resolved with tm_isdst = -1, which
 * asks the C library to work the offset out itself.
 *
 * A system clock jump needs no handling, because the cache is keyed on the
 * timestamp passed in rather than on wall clock time: a jump simply lands
 * outside the cached window. A timezone switch does need handling, since it
 * changes the answer for timestamps already inside the window. Naemon
 * itself only switches zones in read_main_config_file(), which resets the
 * cache. Behind that, the cache fingerprints the timezone/daylight/tzname
 * globals tzset() maintains and discards itself when they change -- but
 * zones with different DST rules can share a fingerprint (Europe/Amsterdam
 * and Africa/Tunis), so the fingerprint alone is not enough.
 *
 * tests/test-timeperiod-daycache.c stresses all of the above.
 */
/*
 * Shared, unlocked state: check_time_against_period() and friends must only be
 * called from the main loop. Livestatus, for one, keeps to that and calls them
 * from a timed event rather than from its query threads.
 */
struct day_cache {
	time_t midnight;
	time_t valid_until; /* 0 when this day must not be cached */
	int year;           /* tm_year/tm_mon/tm_wday as localtime_r() reported */
	int mon;            /* them for the tested timestamp, before mktime() */
	int wday;
	struct tm tm;       /* mktime()-normalized tm of midnight, used as scratch base */
	/* fingerprint of the timezone the cached values were computed in */
	long tz_offset;
	int tz_daylight;
	char tz_name[2][32];
};
static struct day_cache day_cache;

/*
 * A cached day is only meaningful for the timezone it was computed in, and
 * the timezone can be switched underneath us by a tzset(). These globals are
 * what tzset() updates, and comparing them is far cheaper than the
 * localtime_r()/mktime() pair they guard. They catch most switches, not all:
 * see above.
 */
static int day_cache_tz_matches(void)
{
	return day_cache.tz_offset == timezone &&
	       day_cache.tz_daylight == daylight &&
	       !strcmp(day_cache.tz_name[0], tzname[0] ? tzname[0] : "") &&
	       !strcmp(day_cache.tz_name[1], tzname[1] ? tzname[1] : "");
}

static void day_cache_store_tz(void)
{
	day_cache.tz_offset = timezone;
	day_cache.tz_daylight = daylight;
	strncpy(day_cache.tz_name[0], tzname[0] ? tzname[0] : "", sizeof(day_cache.tz_name[0]) - 1);
	day_cache.tz_name[0][sizeof(day_cache.tz_name[0]) - 1] = '\0';
	strncpy(day_cache.tz_name[1], tzname[1] ? tzname[1] : "", sizeof(day_cache.tz_name[1]) - 1);
	day_cache.tz_name[1][sizeof(day_cache.tz_name[1]) - 1] = '\0';
}

static const struct day_cache *get_day_cache(time_t when)
{
	struct tm *t, tm_s, next, probe;
	time_t next_midnight, probe_midnight;

	if (day_cache.valid_until != 0 &&
	    when >= day_cache.midnight && when < day_cache.valid_until &&
	    day_cache_tz_matches())
		return &day_cache;

	t = localtime_r((time_t *)&when, &tm_s);
	if (t == NULL)
		return NULL;

	day_cache.year = t->tm_year;
	day_cache.mon = t->tm_mon;
	day_cache.wday = t->tm_wday;

	t->tm_sec = 0;
	t->tm_min = 0;
	t->tm_hour = 0;

	/*
	 * Probe for a DST transition: a local day that is not exactly 86400
	 * seconds long is not safe to cache. Both ends of the probe are
	 * resolved with tm_isdst = -1, because the midnight computed below
	 * inherits its tm_isdst from the timestamp being tested and is
	 * therefore itself shifted on a transition day -- measuring from it
	 * would make such a day look 86400 seconds long.
	 */
	probe = *t;
	probe.tm_isdst = -1;
	probe_midnight = mktime(&probe);

	next = *t;
	next.tm_mday += 1;
	next.tm_isdst = -1;
	next_midnight = mktime(&next);

	day_cache.tm = *t;
	day_cache.midnight = mktime(&day_cache.tm);

	if (probe_midnight != (time_t) -1 && next_midnight != (time_t) -1 &&
	    next_midnight - probe_midnight == 86400 &&
	    day_cache.midnight == probe_midnight)
		day_cache.valid_until = next_midnight;
	else
		day_cache.valid_until = 0;

	day_cache_store_tz();

	return &day_cache;
}

static inline time_t get_midnight(time_t when)
{
	const struct day_cache *dc = get_day_cache(when);

	if (dc == NULL)
		return (time_t)0L;
	return dc->midnight;
}

/*
 * Internal, exported for testing. The day cache is private to this file, and
 * tests cannot include it without defining its globals a second time next to
 * the ones in libnaemon, so they look at it through these two instead.
 */
int _get_day_cache_entry(time_t when, time_t *midnight, int *year, int *mon,
                         int *wday, int *cacheable)
{
	const struct day_cache *dc = get_day_cache(when);

	if (dc == NULL)
		return -1;
	*midnight = dc->midnight;
	*year = dc->year;
	*mon = dc->mon;
	*wday = dc->wday;
	*cacheable = dc->valid_until != 0;
	return 0;
}

void _reset_day_cache(void)
{
	memset(&day_cache, 0, sizeof(day_cache));
}

timerange *_get_matching_timerange(time_t test_time, const timeperiod *tperiod)
{
	daterange *temp_daterange = NULL;
	time_t start_time = (time_t)0L;
	time_t end_time = (time_t)0L;
	unsigned long days = 0L;
	int year = 0;
	int shift = 0;
	time_t midnight = (time_t)0L;
	struct tm *t, tm_s;
	int daterange_type = 0;
	int test_time_year = 0;
	int test_time_mon = 0;
	int test_time_wday = 0;
	const struct day_cache *dc;

	if (tperiod == NULL)
		return NULL;

	dc = get_day_cache(test_time);
	if (dc == NULL)
		return NULL;

	/* tm_s is used as scratch space below, so work on a copy */
	tm_s = dc->tm;
	t = &tm_s;
	test_time_year = dc->year;
	test_time_mon = dc->mon;
	test_time_wday = dc->wday;
	midnight = dc->midnight;

	/**** check exceptions first ****/
	for (daterange_type = 0; daterange_type < DATERANGE_TYPES; daterange_type++) {

		for (temp_daterange = tperiod->exceptions[daterange_type]; temp_daterange != NULL; temp_daterange = temp_daterange->next) {

#ifdef TEST_TIMEPERIODS_A
			printf("TYPE: %d\n", daterange_type);
			printf("TEST:     %lu = %s", (unsigned long)test_time, ctime(&test_time));
			printf("MIDNIGHT: %lu = %s", (unsigned long)midnight, ctime(&midnight));
#endif

			/* get the start time */
			switch (daterange_type) {
			case DATERANGE_CALENDAR_DATE:
				t->tm_sec = 0;
				t->tm_min = 0;
				t->tm_hour = 0;
				t->tm_wday = 0;
				t->tm_mday = temp_daterange->smday;
				t->tm_mon = temp_daterange->smon;
				t->tm_year = (temp_daterange->syear - 1900);
				t->tm_isdst = -1;
				start_time = mktime(t);
				break;
			case DATERANGE_MONTH_DATE:
				start_time = calculate_time_from_day_of_month(test_time_year, temp_daterange->smon, temp_daterange->smday);
				break;
			case DATERANGE_MONTH_DAY:
				start_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, temp_daterange->smday);
				break;
			case DATERANGE_MONTH_WEEK_DAY:
				start_time = calculate_time_from_weekday_of_month(test_time_year, temp_daterange->smon, temp_daterange->swday, temp_daterange->swday_offset);
				break;
			case DATERANGE_WEEK_DAY:
				start_time = calculate_time_from_weekday_of_month(test_time_year, test_time_mon, temp_daterange->swday, temp_daterange->swday_offset);
				break;
			default:
				continue;
				break;
			}

			/* get the end time */
			switch (daterange_type) {
			case DATERANGE_CALENDAR_DATE:
				t->tm_sec = 0;
				t->tm_min = 0;
				t->tm_hour = 0;
				t->tm_wday = 0;
				t->tm_mday = temp_daterange->emday;
				t->tm_mon = temp_daterange->emon;
				t->tm_year = (temp_daterange->eyear - 1900);
				t->tm_isdst = -1;
				end_time = mktime(t);
				break;
			case DATERANGE_MONTH_DATE:
				year = test_time_year;
				end_time = calculate_time_from_day_of_month(year, temp_daterange->emon, temp_daterange->emday);
				/* advance a year if necessary: august 2 - february 5 */
				if (end_time < start_time) {
					year++;
					end_time = calculate_time_from_day_of_month(year, temp_daterange->emon, temp_daterange->emday);
				}
				break;
			case DATERANGE_MONTH_DAY:
				end_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, temp_daterange->emday);
				break;
			case DATERANGE_MONTH_WEEK_DAY:
				year = test_time_year;
				end_time = calculate_time_from_weekday_of_month(year, temp_daterange->emon, temp_daterange->ewday, temp_daterange->ewday_offset);
				/* advance a year if necessary: thursday 2 august - monday 3 february */
				if (end_time < start_time) {
					year++;
					end_time = calculate_time_from_weekday_of_month(year, temp_daterange->emon, temp_daterange->ewday, temp_daterange->ewday_offset);
				}
				break;
			case DATERANGE_WEEK_DAY:
				end_time = calculate_time_from_weekday_of_month(test_time_year, test_time_mon, temp_daterange->ewday, temp_daterange->ewday_offset);
				break;
			default:
				continue;
				break;
			}

#ifdef TEST_TIMEPERIODS_A
			printf("START:    %lu = %s", (unsigned long)start_time, ctime(&start_time));
			printf("END:      %lu = %s", (unsigned long)end_time, ctime(&end_time));
#endif

			/* start date was bad, so skip this date range */
			if ((unsigned long)start_time == 0L)
				continue;

			/* end date was bad - see if we can handle the error */
			if ((unsigned long)end_time == 0L) {
				switch (daterange_type) {
				case DATERANGE_CALENDAR_DATE:
					continue;
					break;
				case DATERANGE_MONTH_DATE:
					/* end date can't be helped, so skip it */
					if (temp_daterange->emday < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					/* use same year calculated above */
					end_time = calculate_time_from_day_of_month(year, temp_daterange->emon, -1);
					break;
				case DATERANGE_MONTH_DAY:
					/* end date can't be helped, so skip it */
					if (temp_daterange->emday < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					end_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, -1);
					break;
				case DATERANGE_MONTH_WEEK_DAY:
					/* end date can't be helped, so skip it */
					if (temp_daterange->ewday_offset < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					/* use same year calculated above */
					end_time = calculate_time_from_day_of_month(year, test_time_mon, -1);
					break;
				case DATERANGE_WEEK_DAY:
					/* end date can't be helped, so skip it */
					if (temp_daterange->ewday_offset < 0)
						continue;

					/* else end date slipped past end of month, so use last day of month as end date */
					end_time = calculate_time_from_day_of_month(test_time_year, test_time_mon, -1);
					break;
				default:
					continue;
					break;
				}
			}

			/* calculate skip date start (and end) */
			if (temp_daterange->skip_interval > 1) {

				/* skip start date must be before test time */
				if (start_time > test_time)
					continue;

				/* check if interval is across dlst change and gets the compensation */
				shift = get_dst_shift(&start_time, &midnight);

				/* how many days have passed between skip start date and test time? */
				days = (shift + (unsigned long)midnight - (unsigned long)start_time) / (3600 * 24);

				/* if test date doesn't fall on a skip interval day, bail out early */
				if ((days % temp_daterange->skip_interval) != 0)
					continue;

				/* use midnight of test date as start time */
				else
					start_time = midnight;

				/* if skipping range has no end, use test date as end */
				if ((daterange_type == DATERANGE_CALENDAR_DATE) && (is_daterange_single_day(temp_daterange) == TRUE))
					end_time = midnight;
			}

#ifdef TEST_TIMEPERIODS_A
			printf("NEW START:    %lu = %s", (unsigned long)start_time, ctime(&start_time));
			printf("NEW END:      %lu = %s", (unsigned long)end_time, ctime(&end_time));
			printf("%lu DAYS PASSED\n", days);
			printf("DLST SHIFT:   %i\n", shift);
#endif

			/* time falls inside the range of days
			 * end time < start_time when range covers end-of-$unit
			 * (fe. end-of-month) */

			if (((midnight >= start_time && (midnight <= end_time || start_time > end_time)) || (midnight <= end_time && start_time > end_time))) {
#ifdef TEST_TIMEPERIODS_A
				printf("(MATCH)\n");
#endif
				return temp_daterange->times;
			}
		}
	}

	return tperiod->days[test_time_wday];
}

static int is_time_excluded(time_t when, const struct timeperiod *tp)
{
	struct timeperiodexclusion *exc;

	for (exc = tp->exclusions; exc; exc = exc->next) {
		if (check_time_against_period(when, exc->timeperiod_ptr) == OK) {
			return 1;
		}
	}
	return 0;
}

static inline int timerange_includes_time(struct timerange *range, time_t when)
{
	return (when >= (time_t)range->range_start && when < (time_t)range->range_end);
}

/* see if the specified time falls into a valid time range in the given time period */
int check_time_against_period(time_t test_time, const timeperiod *tperiod)
{
	timerange *temp_timerange = NULL;
	time_t midnight = (time_t)0L;

	/* if no period was specified, assume the time is good */
	if (tperiod == NULL)
		return OK;

	if (is_time_excluded(test_time, tperiod))
		return ERROR;

	midnight = get_midnight(test_time);
	for (temp_timerange = _get_matching_timerange(test_time, tperiod); temp_timerange != NULL; temp_timerange = temp_timerange->next) {
		if (timerange_includes_time(temp_timerange, test_time - midnight))
			return OK;
	}
	return ERROR;
}


/* calculate the next time this period ends */
void get_next_invalid_time(time_t pref_time, time_t *invalid_time, const timeperiod *tperiod)
{
	timeperiodexclusion *temp_timeperiodexclusion = NULL;
	int depth = 0;
	int max_depth = 300; // commonly roughly equal to "days in the future"
	struct tm *t, tm_s;
	time_t earliest_time = pref_time;
	time_t last_earliest_time = 0;
	time_t midnight = (time_t)0L;
	time_t day_range_start = (time_t)0L;
	time_t day_range_end = (time_t)0L;
	time_t potential_time = 0;
	time_t excluded_time = 0;
	time_t last_range_end = 0;
	int have_earliest_time = FALSE;
	timerange *last_range = NULL, *temp_timerange = NULL;

	/* if no period was specified, assume the time is good */
	if (tperiod == NULL || check_time_against_period(pref_time, tperiod) == ERROR) {
		*invalid_time = pref_time;
		return;
	}

	/* first excluded time may well be the time we're looking for */
	for (temp_timeperiodexclusion = tperiod->exclusions; temp_timeperiodexclusion != NULL; temp_timeperiodexclusion = temp_timeperiodexclusion->next) {
		/* if pref_time is excluded, we're done */
		if (check_time_against_period(pref_time, temp_timeperiodexclusion->timeperiod_ptr) != ERROR) {
			*invalid_time = pref_time;
			return;
		}
		_get_next_valid_time(pref_time, &potential_time, temp_timeperiodexclusion->timeperiod_ptr);
		if (!excluded_time || excluded_time > potential_time)
			excluded_time = potential_time;
	}

	while (earliest_time != last_earliest_time && depth < max_depth) {
		have_earliest_time = FALSE;
		depth++;
		last_earliest_time = earliest_time;

		t = localtime_r((time_t *)&earliest_time, &tm_s);
		t->tm_sec = 0;
		t->tm_min = 0;
		t->tm_hour = 0;
		midnight = mktime(t);

		temp_timerange = _get_matching_timerange(earliest_time, tperiod);

		for (; temp_timerange; last_range = temp_timerange, temp_timerange = temp_timerange->next) {
			/* ranges with start/end of zero mean exclude this day */

			day_range_start = (time_t)(midnight + temp_timerange->range_start);
			day_range_end = (time_t)(midnight + temp_timerange->range_end);

#ifdef TEST_TIMEPERIODS_B
			printf("  INVALID RANGE START: %lu (%lu) = %s", temp_timerange->range_start, (unsigned long)day_range_start, ctime(&day_range_start));
			printf("  INVALID RANGE END:   %lu (%lu) = %s", temp_timerange->range_end, (unsigned long)day_range_end, ctime(&day_range_end));
#endif

			if (temp_timerange->range_start == 0 && temp_timerange->range_end == 0)
				continue;

			if (excluded_time && day_range_end > excluded_time) {
				earliest_time = excluded_time;
				have_earliest_time = TRUE;
				break;
			}

			/*
			 * Unless two consecutive days have adjoining timeranges,
			 * the end of the last period is the start of the first
			 * invalid time. This only needs special-casing when the
			 * last range of the previous day ends at midnight, and
			 * also catches the special case when there are only
			 * exceptions in a timeperiod and some days are skipped
			 * entirely.
			 */
			if (last_range && last_range->range_end == SECS_PER_DAY && last_range_end && day_range_start != last_range_end) {
				earliest_time = last_range_end;
				have_earliest_time = TRUE;
				break;
			}

			/* stash this day_range_end in case we skip a day */
			last_range_end = day_range_end;

			if (pref_time <= day_range_end && temp_timerange->range_end != SECS_PER_DAY) {
				earliest_time = day_range_end;
				have_earliest_time = TRUE;
#ifdef TEST_TIMEPERIODS_B
				printf("    EARLIEST INVALID TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif
				break;
			}
		}

		/* if we found this in the exclusions, we're done */
		if (have_earliest_time == TRUE) {
			break;
		}

		earliest_time = midnight + SECS_PER_DAY;
	}
#ifdef TEST_TIMEPERIODS_B
	printf("    FINAL EARLIEST INVALID TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif

	if (depth == max_depth)
		*invalid_time = pref_time;
	else
		*invalid_time = earliest_time;
}


/* Separate this out from public get_next_valid_time for testing */
void _get_next_valid_time(time_t pref_time, time_t *valid_time, const timeperiod *tperiod)
{
	timeperiodexclusion *temp_timeperiodexclusion = NULL;
	int depth = 0;
	int max_depth = 300; // commonly roughly equal to "days in the future"
	time_t earliest_time = pref_time;
	time_t last_earliest_time = 0;
	struct tm *t, tm_s;
	time_t midnight = (time_t)0L;
	time_t day_range_start = (time_t)0L;
	time_t day_range_end = (time_t)0L;
	int have_earliest_time = FALSE;
	timerange *temp_timerange = NULL;

	/* if no period was specified, assume the time is good */
	if (tperiod == NULL) {
		*valid_time = pref_time;
		return;
	}

	while (earliest_time != last_earliest_time && depth < max_depth) {
		time_t potential_time = pref_time;
		have_earliest_time = FALSE;
		depth++;
		last_earliest_time = earliest_time;

		t = localtime_r((time_t *)&earliest_time, &tm_s);
		t->tm_sec = 0;
		t->tm_min = 0;
		t->tm_hour = 0;
		midnight = mktime(t);

		temp_timerange = _get_matching_timerange(earliest_time, tperiod);
#ifdef TEST_TIMEPERIODS_B
		printf("  RANGE START: %lu\n", temp_timerange ? temp_timerange->range_start : 0);
		printf("  RANGE END:   %lu\n", temp_timerange ? temp_timerange->range_end : 0);
#endif

		for (; temp_timerange != NULL; temp_timerange = temp_timerange->next) {
			depth++;
			/* ranges with start/end of zero mean exclude this day */
			if (temp_timerange->range_start == 0 && temp_timerange->range_end == 0) {
				continue;
			}

			day_range_start = (time_t)(midnight + temp_timerange->range_start);
			day_range_end = (time_t)(midnight + temp_timerange->range_end);

#ifdef TEST_TIMEPERIODS_B
			printf("  RANGE START: %lu (%lu) = %s", temp_timerange->range_start, (unsigned long)day_range_start, ctime(&day_range_start));
			printf("  RANGE END:   %lu (%lu) = %s", temp_timerange->range_end, (unsigned long)day_range_end, ctime(&day_range_end));
#endif

			/* range is out of bounds */
			if (day_range_end <= last_earliest_time) {
				continue;
			}

			/* preferred time occurs before range start, so use range start time as earliest potential time */
			if (day_range_start >= last_earliest_time)
				potential_time = day_range_start;
			/* preferred time occurs between range start/end, so use preferred time as earliest potential time */
			else if (day_range_end >= last_earliest_time)
				potential_time = last_earliest_time;

			/* is this the earliest time found thus far? */
			if (have_earliest_time == FALSE || potential_time < earliest_time) {
				earliest_time = potential_time;
				have_earliest_time = TRUE;
#ifdef TEST_TIMEPERIODS_B
				printf("    EARLIEST TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif
			}
		}

		if (have_earliest_time == FALSE) {
			earliest_time = midnight + SECS_PER_DAY;
		} else {
			time_t max_excluded = 0, excluded_time = 0;
			for (temp_timeperiodexclusion = tperiod->exclusions; temp_timeperiodexclusion != NULL; temp_timeperiodexclusion = temp_timeperiodexclusion->next) {
				if (check_time_against_period(earliest_time, temp_timeperiodexclusion->timeperiod_ptr) == ERROR) {
					continue;
				}
				get_next_invalid_time(earliest_time, &excluded_time, temp_timeperiodexclusion->timeperiod_ptr);
				if (!max_excluded || max_excluded < excluded_time) {
					max_excluded = excluded_time;
					earliest_time = excluded_time;
				}
			}
			if (max_excluded)
				earliest_time = max_excluded;
#ifdef TEST_TIMEPERIODS_B
			printf("    FINAL EARLIEST TIME: %lu = %s", (unsigned long)earliest_time, ctime(&earliest_time));
#endif
		}
	}

	if (depth == max_depth)
		*valid_time = pref_time;
	else
		*valid_time = earliest_time;
}


/* given a preferred time, get the next valid time within a time period */
void get_next_valid_time(time_t pref_time, time_t *valid_time, const timeperiod *tperiod)
{
	time_t current_time = (time_t)0L;

	/* get time right now, preferred time must be now or in the future */
	time(&current_time);

	pref_time = (pref_time < current_time) ? current_time : pref_time;

	_get_next_valid_time(pref_time, valid_time, tperiod);
}


/* tests if a date range covers just a single day */
static int is_daterange_single_day(daterange *dr)
{

	if (dr == NULL)
		return FALSE;

	if (dr->syear != dr->eyear)
		return FALSE;
	if (dr->smon != dr->emon)
		return FALSE;
	if (dr->smday != dr->emday)
		return FALSE;
	if (dr->swday != dr->ewday)
		return FALSE;
	if (dr->swday_offset != dr->ewday_offset)
		return FALSE;

	return TRUE;
}


/* returns a time (midnight) of particular (3rd, last) day in a given month */
static time_t calculate_time_from_day_of_month(int year, int month, int monthday)
{
	time_t midnight;
	int day = 0;
	struct tm t;

#ifdef TEST_TIMEPERIODS
	printf("YEAR: %d, MON: %d, MDAY: %d\n", year, month, monthday);
#endif

	/* positive day (3rd day) */
	if (monthday > 0) {

		t.tm_sec = 0;
		t.tm_min = 0;
		t.tm_hour = 0;
		t.tm_year = year;
		t.tm_mon = month;
		t.tm_mday = monthday;
		t.tm_isdst = -1;

		midnight = mktime(&t);

#ifdef TEST_TIMEPERIODS
		printf("MIDNIGHT CALC: %s", ctime(&midnight));
#endif

		/* if we rolled over to the next month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	/* negative offset (last day, 3rd to last day) */
	else {
		/* find last day in the month */
		day = 32;
		do {
			/* back up a day */
			day--;

			/* make the new time */
			t.tm_mon = month;
			t.tm_year = year;
			t.tm_mday = day;
			t.tm_isdst = -1;
			midnight = mktime(&t);

		} while (t.tm_mon != month);

		/* now that we know the last day, back up more */
		/* make the new time */
		t.tm_mon = month;
		t.tm_year = year;
		/* -1 means last day of month, so add one to make this correct - Mike Bird */
		t.tm_mday += (monthday < -30) ? -30 : monthday + 1;
		t.tm_isdst = -1;
		midnight = mktime(&t);

		/* if we rolled over to the previous month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	return midnight;
}


/* returns a time (midnight) of particular (3rd, last) weekday in a given month */
static time_t calculate_time_from_weekday_of_month(int year, int month, int weekday, int weekday_offset)
{
	time_t midnight;
	int days = 0;
	int weeks = 0;
	struct tm t;

	t.tm_sec = 0;
	t.tm_min = 0;
	t.tm_hour = 0;
	t.tm_year = year;
	t.tm_mon = month;
	t.tm_mday = 1;
	t.tm_isdst = -1;

	midnight = mktime(&t);

	/* how many days must we advance to reach the first instance of the weekday this month? */
	days = weekday - (t.tm_wday);
	if (days < 0)
		days += 7;

	/* positive offset (3rd thursday) */
	if (weekday_offset > 0) {

		/* how many weeks must we advance (no more than 5 possible) */
		weeks = (weekday_offset > 5) ? 5 : weekday_offset;
		days += ((weeks - 1) * 7);

		/* make the new time */
		t.tm_mon = month;
		t.tm_year = year;
		t.tm_mday = days + 1;
		t.tm_isdst = -1;
		midnight = mktime(&t);

		/* if we rolled over to the next month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	/* negative offset (last thursday, 3rd to last tuesday) */
	else {
		/* find last instance of weekday in the month */
		days += (5 * 7);
		do {
			/* back up a week */
			days -= 7;

			/* make the new time */
			t.tm_mon = month;
			t.tm_year = year;
			t.tm_mday = days + 1;
			t.tm_isdst = -1;
			midnight = mktime(&t);

		} while (t.tm_mon != month);

		/* now that we know the last instance of the weekday, back up more */
		weeks = (weekday_offset < -5) ? -5 : weekday_offset;
		days = ((weeks + 1) * 7);

		/* make the new time */
		t.tm_mon = month;
		t.tm_year = year;
		t.tm_mday += days;
		t.tm_isdst = -1;
		midnight = mktime(&t);

		/* if we rolled over to the previous month, time is invalid */
		/* assume the user's intention is to keep it in the current month */
		if (t.tm_mon != month)
			midnight = (time_t)0L;
	}

	return midnight;
}
