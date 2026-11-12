#include <config.h>
#include "common.h"
#include "defaults.h"
#include "nm_alloc.h"
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <glib.h>
#include <ctype.h>

/*
 * This file holds random utility functions shared by cgi's and
 * core, as well as all global variables needed by both.
 */
int date_format = DATE_FORMAT_US;
int interval_length = DEFAULT_INTERVAL_LENGTH;
char *illegal_output_chars = NULL;
char illegal_output_char_map[] = CHAR_MAP_INIT(0);
time_t program_start = 0L;
int check_external_commands;
int log_rotation_method = LOG_ROTATION_NONE;

char *object_cache_file;
struct object_count num_objects;

int process_performance_data = DEFAULT_PROCESS_PERFORMANCE_DATA;
char *status_file = NULL;

int nagios_pid = 0;
int daemon_mode = FALSE;

time_t last_log_rotation = 0L;

int check_external_commands = DEFAULT_CHECK_EXTERNAL_COMMANDS;

int enable_timing_point = FALSE; /* cgi's never set this to TRUE */

int enable_flap_detection = DEFAULT_ENABLE_FLAP_DETECTION;
int enable_notifications = TRUE;
int execute_service_checks = TRUE;
int accept_passive_service_checks = TRUE;
int execute_host_checks = TRUE;
int accept_passive_host_checks = TRUE;
int enable_event_handlers = TRUE;
int obsess_over_services = FALSE;
int obsess_over_hosts = FALSE;
unsigned long next_downtime_id = 0;

char *config_file_dir = NULL;
char *config_rel_path = NULL;


/* silly debug-ish helper used to track down hotspots in config parsing */
void timing_point(const char *fmt, ...)
{
	static struct timeval last = {0, 0}, first = {0, 0};
	struct timeval now;
	va_list ap;

	if (!enable_timing_point)
		return;

	if (first.tv_sec == 0) {
		tv_set(&first);
		tv_clone(&last, &first);
		printf("[0.0000 (+0.0000)] ");
	} else {
		tv_set(&now);
		printf("[%.4f (+%.4f)] ", tv_delta_f(&first, &now), tv_delta_f(&last, &now));
		tv_clone(&last, &now);
	}
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}


/* fix the problem with strtok() skipping empty options between tokens */
char *my_strtok(char *buffer, const char *tokens)
{
	char *token_position = NULL;
	char *sequence_head = NULL;
	static char *my_strtok_buffer = NULL;
	static char *original_my_strtok_buffer = NULL;

	if (buffer == NULL) {
		if (my_strtok_buffer == NULL)
			return NULL; /*nothing supplied, nothing stored*/
	} else {
		nm_free(original_my_strtok_buffer);
		my_strtok_buffer = nm_strdup(buffer);
		original_my_strtok_buffer = my_strtok_buffer;
	}

	sequence_head = my_strtok_buffer;

	if (sequence_head[0] == '\x0')
		return NULL;

	token_position = strchr(my_strtok_buffer, tokens[0]);

	if (token_position == NULL) {
		my_strtok_buffer = strchr(my_strtok_buffer, '\x0');
		return sequence_head;
	}

	token_position[0] = '\x0';
	my_strtok_buffer = token_position + 1;

	return sequence_head;
}


/* fixes compiler problems under Solaris, since strsep() isn't included */
/* this code is taken from the glibc source */
char *my_strsep(char **stringp, const char *delim)
{
	char *begin, *end;

	begin = *stringp;
	if (begin == NULL)
		return NULL;

	/* A frequent case is when the delimiter string contains only one
	 * character.  Here we don't need to call the expensive `strpbrk'
	 * function and instead work using `strchr'.  */
	if (delim[0] == '\0' || delim[1] == '\0') {
		char ch = delim[0];

		if (ch == '\0' || begin[0] == '\0')
			end = NULL;
		else {
			if (*begin == ch)
				end = begin;
			else
				end = strchr(begin + 1, ch);
		}
	} else {
		/* find the end of the token.  */
		end = strpbrk(begin, delim);
	}

	if (end) {
		/* terminate the token and set *STRINGP past NUL character.  */
		*end++ = '\0';
		*stringp = end;
	} else
		/* no more delimiters; this is the last token.  */
		*stringp = NULL;

	return begin;
}


/* open a file for reading line by line */
nm_rfile *nm_fopen_ro(const char *filename)
{
	nm_rfile *file;
	struct stat st;
	FILE *fp;

	if (filename == NULL)
		return NULL;

	if ((fp = fopen(filename, "r")) == NULL)
		return NULL;

	if (fstat(fileno(fp), &st) == -1) {
		int err = errno;
		fclose(fp);
		errno = err;
		return NULL;
	}

	file = nm_calloc(1, sizeof(nm_rfile));
	file->path = nm_strdup(filename);
	file->fp = fp;
	/* only a regular file has a size to compare against */
	file->size = S_ISREG(st.st_mode) ? st.st_size : -1;

	return file;
}


/* close a file opened with nm_fopen_ro(), ERROR if it was not read completely */
int nm_fclose(nm_rfile *file)
{
	int result;

	if (file == NULL)
		return ERROR;

	result = nm_ferror(file) ? ERROR : OK;
	fclose(file->fp);
	nm_free(file->path);
	nm_free(file);

	return result;
}


/* gets one line of input, including its newline */
char *nm_fgets(nm_rfile *file)
{
	char *buf = NULL;
	size_t size = 0;
	ssize_t len;

	if (file == NULL)
		return NULL;

	if ((len = getline(&buf, &size, file->fp)) <= 0) {
		free(buf);
		if (ferror(file->fp))
			file->error = errno ? errno : EIO;
		else if (file->size >= 0 && file->bytes_read != file->size)
			file->changed = TRUE;
		return NULL;
	}

	file->bytes_read += len;
	file->current_line++;

	return buf;
}


/* TRUE if reading stopped at a read error or the file changed size meanwhile */
int nm_ferror(const nm_rfile *file)
{
	return file == NULL || file->error || file->changed;
}


/* describes what nm_ferror() found */
const char *nm_ferror_str(const nm_rfile *file)
{
	if (file == NULL)
		return "no file";
	if (file->error)
		return strerror(file->error);
	if (file->changed)
		return "the file changed size while it was being read";
	return "no error";
}


/* gets one line of input (may be contained on more than one line in the source file) */
char *nm_fgets_multiline(nm_rfile *file)
{
	char *buf = NULL;
	char *tempbuf = NULL;
	char *stripped = NULL;
	int len = 0;
	int len2 = 0;
	int end = 0;

	if (file == NULL)
		return NULL;

	while (1) {

		nm_free(tempbuf);

		if ((tempbuf = nm_fgets(file)) == NULL)
			break;

		if (buf == NULL) {
			len = strlen(tempbuf);
			buf = nm_malloc(len + 1);
			memcpy(buf, tempbuf, len);
			buf[len] = '\x0';
		} else {
			/* strip leading white space from continuation lines */
			stripped = tempbuf;
			while (*stripped == ' ' || *stripped == '\t')
				stripped++;
			len = strlen(stripped);
			len2 = strlen(buf);
			buf = nm_realloc(buf, len + len2 + 1);
			strcat(buf, stripped);
			len += len2;
			buf[len] = '\x0';
		}

		if (len == 0)
			break;

		/* handle Windows/DOS CR/LF */
		if (len >= 2 && buf[len - 2] == '\r')
			end = len - 3;
		/* normal Unix LF */
		else if (len >= 1 && buf[len - 1] == '\n')
			end = len - 2;
		else
			end = len - 1;

		/* two backslashes found. unescape first backslash first and break */
		if (end >= 1 && buf[end - 1] == '\\' && buf[end] == '\\') {
			buf[end] = '\n';
			buf[end + 1] = '\x0';
			break;
		}

		/* one backslash found. continue reading the next line */
		else if (end > 0 && buf[end] == '\\')
			buf[end] = '\x0';

		/* no continuation marker was found, so break */
		else
			break;
	}

	nm_free(tempbuf);

	return buf;
}


/* strip newline, carriage return, and tab characters from beginning and end of a string */
void strip(char *buffer)
{
	register int x, z;
	int len;

	if (buffer == NULL || buffer[0] == '\x0')
		return;

	/* strip end of string */
	len = (int)strlen(buffer);
	for (x = len - 1; x >= 0; x--) {
		switch (buffer[x]) {
		case ' ':
		case '\n':
		case '\r':
		case '\t':
			buffer[x] = '\x0';
			continue;
		}
		break;
	}

	/* if we stripped all of it, just return */
	if (!x)
		return;

	/* save last position for later... */
	z = x;

	/* strip beginning of string (by shifting) */
	/* NOTE: this is very expensive to do, so avoid it whenever possible */
	for (x = 0;; x++) {
		switch (buffer[x]) {
		case ' ':
		case '\n':
		case '\r':
		case '\t':
			continue;
		}
		break;
	}

	if (x > 0 && z > 0) {
		/* new length of the string after we stripped the end */
		len = z + 1;

		/* shift chars towards beginning of string to remove leading whitespace */
		for (z = x; z < len; z++)
			buffer[z - x] = buffer[z];
		buffer[len - x] = '\x0';
	}
}

// strip trailing whitespace, returns pointer to stripped string
char *rstrip(char *c)
{
    char *w = c + strlen(c) - 1;
    while (w >= c && isspace(*w))
        *w-- = '\0';
    return c;
}

// strip trailing whitespace, returns pointer to stripped string
// NOTE: you need to free the original pointer, not the stripped one
char *lstrip(char *c)
{
    while (isspace(*c)) c++;
    return c;
}

// trim leading/trailing whitespace, returns pointer to stripped string
// NOTE: you need to free the original pointer, not the stripped one
char *trim(char *c)
{
    return(lstrip(rstrip(c)));
}

void noeol_ctime(const time_t *when, char *buf)
{
	ctime_r(when, buf);
	buf[strlen(buf) - 1] = 0;
}

/*
 * given a date/time in time_t format, produce a corresponding
 * date/time string, including timezone
 */
void get_datetime_string(time_t *raw_time, char *buffer, int buffer_length,
                         int type)
{
	time_t t;
	struct tm *tm_ptr, tm_s;
	int hour;
	int minute;
	int second;
	int month;
	int day;
	int year;
	const char *weekdays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	const char *months[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sept",
		"Oct", "Nov", "Dec"
	};
	const char *tzone = "";

	if (raw_time == NULL)
		time(&t);
	else
		t = *raw_time;

	if (type == HTTP_DATE_TIME)
		tm_ptr = gmtime_r(&t, &tm_s);
	else
		tm_ptr = localtime_r(&t, &tm_s);

	hour = tm_ptr->tm_hour;
	minute = tm_ptr->tm_min;
	second = tm_ptr->tm_sec;
	month = tm_ptr->tm_mon + 1;
	day = tm_ptr->tm_mday;
	year = tm_ptr->tm_year + 1900;

#ifdef HAVE_TM_ZONE
	tzone = tm_ptr->tm_zone;
#else
	tzone = (tm_ptr->tm_isdst) ? tzname[1] : tzname[0];
#endif

	/* ctime() style date/time */
	if (type == LONG_DATE_TIME)
		snprintf(buffer, buffer_length, "%s %s %d %02d:%02d:%02d %s %d",
		         weekdays[tm_ptr->tm_wday], months[tm_ptr->tm_mon], day,
		         hour, minute, second, tzone, year);

	/* short date/time */
	else if (type == SHORT_DATE_TIME) {
		if (date_format == DATE_FORMAT_EURO)
			snprintf(buffer, buffer_length,
			         "%02d-%02d-%04d %02d:%02d:%02d", day, month,
			         year, hour, minute, second);
		else if (date_format == DATE_FORMAT_ISO8601
		         || date_format == DATE_FORMAT_STRICT_ISO8601)
			snprintf(buffer, buffer_length,
			         "%04d-%02d-%02d%c%02d:%02d:%02d", year, month,
			         day,
			         (date_format ==
			          DATE_FORMAT_STRICT_ISO8601) ? 'T' : ' ', hour,
			         minute, second);
		else
			snprintf(buffer, buffer_length,
			         "%02d-%02d-%04d %02d:%02d:%02d", month, day,
			         year, hour, minute, second);
	}

	/* short date */
	else if (type == SHORT_DATE) {
		if (date_format == DATE_FORMAT_EURO)
			snprintf(buffer, buffer_length, "%02d-%02d-%04d", day,
			         month, year);
		else if (date_format == DATE_FORMAT_ISO8601
		         || date_format == DATE_FORMAT_STRICT_ISO8601)
			snprintf(buffer, buffer_length, "%04d-%02d-%02d", year,
			         month, day);
		else
			snprintf(buffer, buffer_length, "%02d-%02d-%04d", month,
			         day, year);
	}

	/* expiration date/time for HTTP headers */
	else if (type == HTTP_DATE_TIME)
		snprintf(buffer, buffer_length,
		         "%s, %02d %s %d %02d:%02d:%02d GMT",
		         weekdays[tm_ptr->tm_wday], day, months[tm_ptr->tm_mon],
		         year, hour, minute, second);

	/* short time */
	else
		snprintf(buffer, buffer_length, "%02d:%02d:%02d", hour, minute,
		         second);

	buffer[buffer_length - 1] = '\x0';
}


/* get days, hours, minutes, and seconds from a raw time_t format or total seconds */
void get_time_breakdown(unsigned long raw_time, int *days, int *hours,
                        int *minutes, int *seconds)
{
	unsigned long temp_time;
	int temp_days;
	int temp_hours;
	int temp_minutes;
	int temp_seconds;

	temp_time = raw_time;

	temp_days = temp_time / 86400;
	temp_time -= (temp_days * 86400);
	temp_hours = temp_time / 3600;
	temp_time -= (temp_hours * 3600);
	temp_minutes = temp_time / 60;
	temp_time -= (temp_minutes * 60);
	temp_seconds = (int)temp_time;

	*days = temp_days;
	*hours = temp_hours;
	*minutes = temp_minutes;
	*seconds = temp_seconds;
}

gint my_strsorter(gconstpointer a, gconstpointer b, gpointer data)
{
	return (g_strcmp0(a, b));
}
