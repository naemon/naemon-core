#include "objects_timeperiod.h"
#include "nm_alloc.h"
#include "logging.h"
#include <string.h>
#include <glib.h>

static GHashTable *timeperiod_hash_table = NULL;
timeperiod **timeperiod_ary = NULL;
timeperiod *timeperiod_list = NULL;

int init_objects_timeperiod(int elems)
{
	timeperiod_ary = nm_calloc(elems, sizeof(timeperiod*));
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

	new_timeperiod = nm_calloc(1, sizeof(*new_timeperiod));

	/* copy string vars */
	new_timeperiod->name = nm_strdup(name);
	new_timeperiod->alias = alias ? nm_strdup(alias) : new_timeperiod->name;

	return new_timeperiod;
}

int register_timeperiod(timeperiod *new_timeperiod)
{
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

	/* make sure we have enough data */
	if (period == NULL || name == NULL)
		return NULL;

	new_timeperiodexclusion = nm_malloc(sizeof(timeperiodexclusion));
	new_timeperiodexclusion->timeperiod_name = nm_strdup(name);

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

	/* allocate memory for the date range range */
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
	return g_hash_table_lookup(timeperiod_hash_table, name);
}

static const char *timerange2str(const timerange *tr)
{
	static char str[12];
	int sh, sm, eh, em;

	if (!tr)
		return "";
	sh = tr->range_start / 3600;
	sm = (tr->range_start / 60) % 60;
	eh = tr->range_end / 3600;
	em = (tr->range_end / 60) % 60;
	sprintf(str, "%02d:%02d-%02d:%02d", sh, sm, eh, em);
	return str;
}

void fcache_timeperiod(FILE *fp, timeperiod *temp_timeperiod)
{
	const char *days[7] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
	const char *months[12] = {"january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december"};
	daterange *temp_daterange;
	timerange *tr;
	register int x;

	fprintf(fp, "define timeperiod {\n");
	fprintf(fp, "\ttimeperiod_name\t%s\n", temp_timeperiod->name);
	if (temp_timeperiod->alias)
		fprintf(fp, "\talias\t%s\n", temp_timeperiod->alias);

	if (temp_timeperiod->exclusions) {
		timeperiodexclusion *exclude;
		fprintf(fp, "\texclude\t");
		for (exclude = temp_timeperiod->exclusions; exclude; exclude = exclude->next) {
			fprintf(fp, "%s%c", exclude->timeperiod_name, exclude->next ? ',' : '\n');
		}
	}

	for (x = 0; x < DATERANGE_TYPES; x++) {
		for (temp_daterange = temp_timeperiod->exceptions[x]; temp_daterange != NULL; temp_daterange = temp_daterange->next) {

			/* skip null entries */
			if (temp_daterange->times == NULL)
				continue;

			switch (temp_daterange->type) {
			case DATERANGE_CALENDAR_DATE:
				fprintf(fp, "\t%d-%02d-%02d", temp_daterange->syear, temp_daterange->smon + 1, temp_daterange->smday);
				if ((temp_daterange->smday != temp_daterange->emday) || (temp_daterange->smon != temp_daterange->emon) || (temp_daterange->syear != temp_daterange->eyear))
					fprintf(fp, " - %d-%02d-%02d", temp_daterange->eyear, temp_daterange->emon + 1, temp_daterange->emday);
				if (temp_daterange->skip_interval > 1)
					fprintf(fp, " / %d", temp_daterange->skip_interval);
				break;
			case DATERANGE_MONTH_DATE:
				fprintf(fp, "\t%s %d", months[temp_daterange->smon], temp_daterange->smday);
				if ((temp_daterange->smon != temp_daterange->emon) || (temp_daterange->smday != temp_daterange->emday)) {
					fprintf(fp, " - %s %d", months[temp_daterange->emon], temp_daterange->emday);
					if (temp_daterange->skip_interval > 1)
						fprintf(fp, " / %d", temp_daterange->skip_interval);
				}
				break;
			case DATERANGE_MONTH_DAY:
				fprintf(fp, "\tday %d", temp_daterange->smday);
				if (temp_daterange->smday != temp_daterange->emday) {
					fprintf(fp, " - %d", temp_daterange->emday);
					if (temp_daterange->skip_interval > 1)
						fprintf(fp, " / %d", temp_daterange->skip_interval);
				}
				break;
			case DATERANGE_MONTH_WEEK_DAY:
				fprintf(fp, "\t%s %d %s", days[temp_daterange->swday], temp_daterange->swday_offset, months[temp_daterange->smon]);
				if ((temp_daterange->smon != temp_daterange->emon) || (temp_daterange->swday != temp_daterange->ewday) || (temp_daterange->swday_offset != temp_daterange->ewday_offset)) {
					fprintf(fp, " - %s %d %s", days[temp_daterange->ewday], temp_daterange->ewday_offset, months[temp_daterange->emon]);
					if (temp_daterange->skip_interval > 1)
						fprintf(fp, " / %d", temp_daterange->skip_interval);
				}
				break;
			case DATERANGE_WEEK_DAY:
				fprintf(fp, "\t%s %d", days[temp_daterange->swday], temp_daterange->swday_offset);
				if ((temp_daterange->swday != temp_daterange->ewday) || (temp_daterange->swday_offset != temp_daterange->ewday_offset)) {
					fprintf(fp, " - %s %d", days[temp_daterange->ewday], temp_daterange->ewday_offset);
					if (temp_daterange->skip_interval > 1)
						fprintf(fp, " / %d", temp_daterange->skip_interval);
				}
				break;
			default:
				break;
			}

			fputc('\t', fp);
			for (tr = temp_daterange->times; tr; tr = tr->next) {
				fprintf(fp, "%s%c", timerange2str(tr), tr->next ? ',' : '\n');
			}
		}
	}
	for (x = 0; x < 7; x++) {
		/* skip null entries */
		if (temp_timeperiod->days[x] == NULL)
			continue;

		fprintf(fp, "\t%s\t", days[x]);
		for (tr = temp_timeperiod->days[x]; tr; tr = tr->next) {
			fprintf(fp, "%s%c", timerange2str(tr), tr->next ? ',' : '\n');
		}
	}
	fprintf(fp, "\t}\n\n");
}
