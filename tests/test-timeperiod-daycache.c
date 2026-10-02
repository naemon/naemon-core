/*
 * Stress test for the day cache in objects_timeperiod.c.
 *
 * The cache exists so that check_time_against_period() does not have to run
 * localtime_r()/mktime() for every dispatched check. It is only ever allowed
 * to return what the uncached computation would have returned, so this test
 * reimplements that uncached computation ("the reference") and compares the
 * two over a large number of timestamps.
 *
 * The interesting cases are days that are not 86400 seconds long or whose
 * local midnight is ambiguous or missing:
 *
 *   - DST spring forward (23h) and fall back (25h)
 *   - zones that shift by 30 or 45 minutes rather than an hour
 *   - zones whose DST transition happens *at* midnight, so local 00:00 does
 *     not exist that day (America/Havana, America/Santiago, Asia/Beirut)
 *   - leap seconds, which under a "right/" zone make a day 86401 seconds
 *   - a leap day, and the day after a zone skipped a calendar day entirely
 *     (Pacific/Apia 2011, Pacific/Kiritimati 1994) -- both of which are
 *     ordinary 86400 second days and *should* be cached
 *
 * Timestamps are visited in several orders on purpose. Sequential access is
 * the normal case, random access catches state left over from an unrelated
 * day, and walking backwards is what the cache sees when the system clock is
 * stepped back.
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>

#include "naemon/objects_timeperiod.h"
#include "naemon/configuration.h"
#include "naemon/utils.h"
#include "naemon/globals.h"
#include "naemon/nm_alloc.h"

/*
 * What the day cache holds for a timestamp. The cache itself is private to
 * objects_timeperiod.c; including that file here would define its globals a
 * second time next to libnaemon's, which AddressSanitizer reports as an ODR
 * violation, so the test looks at it through the exported hooks instead.
 */
struct cached_day {
	time_t midnight;
	int year, mon, wday;
	int cacheable;
};

/* Returns 0 when the lookup failed. */
static int lookup_day(time_t when, struct cached_day *cd)
{
	return _get_day_cache_entry(when, &cd->midnight, &cd->year, &cd->mon,
	                            &cd->wday, &cd->cacheable) == 0;
}

/* 1990-01-01 .. 2036-01-01: spans leap years, leap seconds and the historical
 * timezone oddities the tzdata database records */
#define RANGE_START 631152000L
#define RANGE_SPAN  (46LL * 365 * 86400)

static const char *tricky_timezones[] = {
	"UTC",
	"Europe/Berlin",           /* ordinary one hour DST */
	"Europe/London",           /* DST while sitting on UTC */
	"America/New_York",
	"America/Havana",          /* DST transition at midnight */
	"America/Santiago",        /* DST transition at midnight, southern hemisphere */
	"Asia/Beirut",             /* DST transition at midnight */
	"Africa/Cairo",            /* DST reintroduced in 2023 */
	"Australia/Lord_Howe",     /* 30 minute DST shift */
	"Pacific/Chatham",         /* 45 minute standard offset */
	"Pacific/Apia",            /* skipped 2011-12-30 entirely */
	"Pacific/Kiritimati",      /* skipped 1994-12-31 entirely */
	"Antarctica/Troll",        /* two hour DST shift */
	"Iran",
	"Asia/Kolkata",            /* 30 minute standard offset, no DST */
	"right/Europe/Berlin",     /* counts leap seconds: some days are 86401s */
};

/*
 * The computation as it was before the cache was introduced. Everything the
 * cache hands out has to match this.
 */
struct reference_day {
	time_t midnight;
	int year;
	int mon;
	int wday;
};

static struct reference_day reference_day_for(time_t when)
{
	struct reference_day r;
	struct tm *t, tm_s;

	t = localtime_r(&when, &tm_s);
	r.year = t->tm_year;
	r.mon = t->tm_mon;
	r.wday = t->tm_wday;
	t->tm_sec = 0;
	t->tm_min = 0;
	t->tm_hour = 0;
	r.midnight = mktime(t);
	return r;
}

/* Returns 0 when the cache agrees with the reference for this timestamp. */
static int disagrees_at(time_t when, const char **why)
{
	struct reference_day ref = reference_day_for(when);
	struct cached_day cd;

	if (!lookup_day(when, &cd)) {
		*why = "cache lookup failed";
		return 1;
	}
	if (cd.midnight != ref.midnight) {
		*why = "midnight differs";
		return 1;
	}
	if (cd.year != ref.year) {
		*why = "tm_year differs";
		return 1;
	}
	if (cd.mon != ref.mon) {
		*why = "tm_mon differs";
		return 1;
	}
	if (cd.wday != ref.wday) {
		*why = "tm_wday differs";
		return 1;
	}
	return 0;
}

static void forget_cached_day(void)
{
	_reset_day_cache();
}

/* Describes a timestamp in a way that is useful in a failure message. */
static const char *describe(time_t when)
{
	static char buf[80];
	struct tm tm_s;

	if (localtime_r(&when, &tm_s) == NULL)
		snprintf(buf, sizeof(buf), "%ld", (long)when);
	else
		strftime(buf, sizeof(buf), "%F %T %Z", &tm_s);
	return buf;
}

/*
 * glibc treats an unknown TZ as UTC without complaint, so a zone missing from
 * the system would quietly turn into a UTC test -- and the leap second case
 * would then fail, since a UTC day is perfectly cacheable. Ubuntu 26.04, for
 * one, does not install the right/ zones by default. Skip such a case rather
 * than test the wrong zone.
 */
static int zone_available(const char *tz)
{
	const char *dir = getenv("TZDIR");
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", dir && *dir ? dir : "/usr/share/zoneinfo", tz);
	return access(path, R_OK) == 0;
}

#define SKIP_UNLESS_ZONE_AVAILABLE(tz) \
	do { \
		if (!zone_available(tz)) { \
			fprintf(stderr, "skipping TZ=%s: zone not installed\n", (tz)); \
			return; \
		} \
	} while (0)

static void switch_timezone(const char *tz)
{
	setenv("TZ", tz, 1);
	tzset();
	forget_cached_day();
}

START_TEST(daycache_matches_reference)
{
	const char *tz = tricky_timezones[_i];
	const char *why = NULL;
	time_t t;
	long i;

	SKIP_UNLESS_ZONE_AVAILABLE(tz);
	switch_timezone(tz);

	/* sequential, with a step that is not a divisor of a day so that every
	 * time of day gets visited as the walk progresses */
	for (t = RANGE_START; t < RANGE_START + RANGE_SPAN; t += 4001) {
		if (disagrees_at(t, &why))
			ck_abort_msg("TZ=%s sequential: %s at %s", tz, why, describe(t));
	}

	/* random access */
	srand(20260827);
	for (i = 0; i < 200000; i++) {
		long long r = ((long long)rand() << 31) ^ (long long)rand();
		if (r < 0)
			r = -r;
		t = RANGE_START + (time_t)(r % RANGE_SPAN);
		if (disagrees_at(t, &why))
			ck_abort_msg("TZ=%s random: %s at %s", tz, why, describe(t));
	}

	/* backwards, i.e. what a system clock stepped back looks like */
	for (t = RANGE_START + RANGE_SPAN; t > RANGE_START; t -= 4001) {
		if (disagrees_at(t, &why))
			ck_abort_msg("TZ=%s backwards: %s at %s", tz, why, describe(t));
	}
}
END_TEST

START_TEST(daycache_matches_reference_around_transitions)
{
	const char *tz = tricky_timezones[_i];
	const char *why = NULL;
	time_t day;

	SKIP_UNLESS_ZONE_AVAILABLE(tz);
	switch_timezone(tz);

	/*
	 * Sweep densely across every day whose midnight is not at offset 0 from
	 * the naive UTC day boundary -- that catches DST transitions wherever
	 * they sit -- plus all of February and March, which covers leap days.
	 */
	for (day = RANGE_START; day < RANGE_START + RANGE_SPAN; day += 86400) {
		struct tm tm_s;
		time_t t;

		if (localtime_r(&day, &tm_s) == NULL)
			continue;
		if (tm_s.tm_hour == 0 && tm_s.tm_mon != 1 && tm_s.tm_mon != 2)
			continue;

		for (t = day - 7200; t < day + 100000; t += 137) {
			if (disagrees_at(t, &why))
				ck_abort_msg("TZ=%s transition sweep: %s at %s", tz, why, describe(t));
		}
	}
}
END_TEST

/*
 * A timezone switch has to invalidate the cache: the same timestamp belongs to
 * a different local day afterwards. tzset() is what applies the switch, so the
 * cache watches the globals tzset() maintains.
 */
START_TEST(daycache_notices_timezone_change)
{
	static const char *zones[] = {
		"UTC", "Europe/Berlin", "America/New_York", "Pacific/Chatham",
		"Asia/Kolkata", "Europe/London", "Australia/Lord_Howe",
	};
	/* a fixed instant, deliberately queried again after each switch */
	const time_t when = 1750000000L;
	unsigned lap, i;

	switch_timezone("UTC");

	for (lap = 0; lap < 3; lap++) {
		for (i = 0; i < ARRAY_SIZE(zones); i++) {
			struct reference_day ref;
			struct cached_day cd;

			/* note: no forget_cached_day() here, the cache has to
			 * work this out for itself */
			setenv("TZ", zones[i], 1);
			tzset();

			ref = reference_day_for(when);
			ck_assert_msg(lookup_day(when, &cd), "cache lookup failed for TZ=%s", zones[i]);

			ck_assert_msg(cd.midnight == ref.midnight,
			              "TZ=%s: cache kept a stale midnight from the previous zone "
			              "(got %ld, expected %ld)",
			              zones[i], (long)cd.midnight, (long)ref.midnight);
			ck_assert_msg(cd.wday == ref.wday,
			              "TZ=%s: cache kept a stale weekday", zones[i]);
		}
	}
}
END_TEST

/*
 * Europe/Amsterdam and Africa/Tunis are both CET/CEST with the same offset, so
 * the fingerprint cannot tell them apart, but Tunis has no DST: in July their
 * midnights are an hour apart. Switching between them through use_timezone
 * must still not leave the Amsterdam midnight in the cache.
 */
START_TEST(daycache_reset_by_use_timezone)
{
	/* 2026-07-15 12:00 UTC */
	const time_t when = 1784116800L;
	char cfg[] = "/tmp/daycache-tz-XXXXXX";
	struct reference_day ref;
	struct cached_day cd;
	FILE *fp;
	int fd;

	SKIP_UNLESS_ZONE_AVAILABLE("Europe/Amsterdam");
	SKIP_UNLESS_ZONE_AVAILABLE("Africa/Tunis");

	fd = mkstemp(cfg);
	ck_assert(fd >= 0);
	fp = fdopen(fd, "w");
	ck_assert(fp != NULL);
	fprintf(fp, "use_timezone=Africa/Tunis\n");
	fclose(fp);

	switch_timezone("Europe/Amsterdam");
	ck_assert(lookup_day(when, &cd));

	ck_assert_int_eq(OK, reset_variables());
	config_file_dir = nspath_absolute_dirname(cfg, NULL);
	config_rel_path = nm_strdup(config_file_dir);
	ck_assert_int_eq(OK, read_main_config_file(cfg));
	unlink(cfg);

	/*
	 * Ask the cache first: localtime_r() and mktime() rewrite the tzset()
	 * globals for the time they convert, so computing the reference first
	 * would change the fingerprint and hide a stale entry.
	 */
	ck_assert(lookup_day(when, &cd));
	ref = reference_day_for(when);
	ck_assert_msg(cd.midnight == ref.midnight,
	              "use_timezone switch kept a stale midnight (got %ld, expected %ld)",
	              (long)cd.midnight, (long)ref.midnight);

	nm_free(config_file_dir);
	nm_free(config_rel_path);
}
END_TEST

/*
 * Irregular days must fall through to the uncached path, ordinary ones must be
 * served from the cache. This pins down the classification itself, so that a
 * future change cannot quietly start caching a transition day.
 */
struct classification_case {
	const char *tz;
	int year, mon, mday;
	int expect_cached;
	const char *what;
};

static const struct classification_case classification_cases[] = {
	/* ordinary days, including ones that only look exotic */
	{ "Europe/Berlin",      2024,  2, 29, 1, "leap day" },
	{ "Europe/Berlin",      2023,  2, 28, 1, "non leap year February" },
	{ "Europe/Berlin",      2025,  7,  1, 1, "plain summer day" },
	{ "Pacific/Apia",       2011, 12, 31, 1, "day after the zone skipped 2011-12-30" },
	{ "Pacific/Kiritimati", 1995,  1,  1, 1, "day after the zone skipped 1994-12-31" },
	{ "Asia/Kolkata",       2025,  3, 30, 1, "half hour offset, no DST" },

	/* irregular days, must not be cached */
	{ "Europe/Berlin",      2025,  3, 30, 0, "DST spring forward, 23h" },
	{ "Europe/Berlin",      2025, 10, 26, 0, "DST fall back, 25h" },
	{ "America/Havana",     2025,  3,  9, 0, "DST transition at midnight" },
	{ "America/Santiago",   2025,  9,  7, 0, "DST transition at midnight" },
	{ "Asia/Beirut",        2025,  3, 30, 0, "DST transition at midnight" },
	{ "Australia/Lord_Howe", 2025, 10, 5, 0, "30 minute DST shift" },
	{ "right/Europe/Berlin", 2017, 1,  1, 0, "day carrying a leap second" },
};

START_TEST(daycache_classifies_irregular_days)
{
	const struct classification_case *c = &classification_cases[_i];
	struct tm tm_s;
	time_t noon;
	struct cached_day cd;

	SKIP_UNLESS_ZONE_AVAILABLE(c->tz);
	switch_timezone(c->tz);

	memset(&tm_s, 0, sizeof(tm_s));
	tm_s.tm_year = c->year - 1900;
	tm_s.tm_mon = c->mon - 1;
	tm_s.tm_mday = c->mday;
	tm_s.tm_hour = 12;
	tm_s.tm_isdst = -1;
	noon = mktime(&tm_s);
	ck_assert_msg(noon != (time_t) -1, "could not build %s %04d-%02d-%02d",
	              c->tz, c->year, c->mon, c->mday);

	ck_assert_msg(lookup_day(noon, &cd), "cache lookup failed");

	if (c->expect_cached) {
		ck_assert_msg(cd.cacheable,
		              "TZ=%s %04d-%02d-%02d (%s) should be cacheable but was not",
		              c->tz, c->year, c->mon, c->mday, c->what);
	} else {
		ck_assert_msg(!cd.cacheable,
		              "TZ=%s %04d-%02d-%02d (%s) must not be cached",
		              c->tz, c->year, c->mon, c->mday, c->what);
	}

	/* whatever the classification, the answer still has to be right */
	{
		struct reference_day ref = reference_day_for(noon);
		ck_assert_msg(cd.midnight == ref.midnight,
		              "TZ=%s %04d-%02d-%02d (%s): wrong midnight",
		              c->tz, c->year, c->mon, c->mday, c->what);
	}
}
END_TEST

Suite *daycache_suite(void)
{
	Suite *s = suite_create("Timeperiod day cache");
	TCase *tc_sweep = tcase_create("Agreement with the uncached computation");
	TCase *tc_class = tcase_create("Classification of irregular days");

	/* the sweeps are deliberately large; the default 4s timeout is not enough */
	tcase_set_timeout(tc_sweep, 300);
	tcase_add_loop_test(tc_sweep, daycache_matches_reference,
	                    0, ARRAY_SIZE(tricky_timezones));
	tcase_add_loop_test(tc_sweep, daycache_matches_reference_around_transitions,
	                    0, ARRAY_SIZE(tricky_timezones));
	tcase_add_test(tc_sweep, daycache_notices_timezone_change);
	tcase_add_test(tc_sweep, daycache_reset_by_use_timezone);
	suite_add_tcase(s, tc_sweep);

	tcase_add_loop_test(tc_class, daycache_classifies_irregular_days,
	                    0, ARRAY_SIZE(classification_cases));
	suite_add_tcase(s, tc_class);

	return s;
}

int main(void)
{
	int failed;
	SRunner *sr = srunner_create(daycache_suite());

	srunner_run_all(sr, CK_ENV);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
