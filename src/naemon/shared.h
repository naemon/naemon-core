#ifndef INCLUDE__shared_h__
#define INCLUDE__shared_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include "lib/libnaemon.h"
#include <glib.h>

NAGIOS_BEGIN_DECL

/* a file opened with nm_fopen_ro() */
typedef struct nm_rfile {
	char *path;
	FILE *fp;
	unsigned long current_line;
	off_t size;           /* at open, -1 if not a regular file */
	off_t bytes_read;
	int error;            /* errno of a read error */
	int changed;          /* size differed from bytes read at end of file */
} nm_rfile;

/* official count of first-class objects */
struct object_count {
	unsigned int commands;
	unsigned int timeperiods;
	unsigned int hosts;
	unsigned int hostescalations;
	unsigned int hostdependencies;
	unsigned int services;
	unsigned int serviceescalations;
	unsigned int servicedependencies;
	unsigned int contacts;
	unsigned int contactgroups;
	unsigned int hostgroups;
	unsigned int servicegroups;
};

extern struct object_count num_objects;

void timing_point(const char *fmt, ...); /* print a message and the time since the first message */
char *my_strtok(char *buffer, const char *tokens);
char *my_strsep(char **stringp, const char *delim);
nm_rfile *nm_fopen_ro(const char *filename);
int nm_fclose(nm_rfile *file);
char *nm_fgets(nm_rfile *file);
char *nm_fgets_multiline(nm_rfile *file);
int nm_ferror(const nm_rfile *file);
const char *nm_ferror_str(const nm_rfile *file);
void strip(char *buffer);
char *rstrip(char *c);
char *lstrip(char *c);
char *trim(char *c);
void noeol_ctime(const time_t *, char *);
void get_datetime_string(time_t *raw_time, char *buffer,
                                int buffer_length, int type);
void get_time_breakdown(unsigned long raw_time, int *days, int *hours,
                               int *minutes, int *seconds);
gint my_strsorter(gconstpointer a, gconstpointer b, gpointer data);

NAGIOS_END_DECL
#endif
