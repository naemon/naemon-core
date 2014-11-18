#ifndef INCLUDE__shared_h__
#define INCLUDE__shared_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/libnaemon.h"

NAGIOS_BEGIN_DECL

/* mmapfile structure - used for reading files via mmap() */
typedef struct mmapfile_struct {
	char *path;
	int mode;
	int fd;
	unsigned long file_size;
	unsigned long current_position;
	unsigned long current_line;
	void *mmap_buf;
} mmapfile;

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
mmapfile *mmap_fopen(const char *filename);
int mmap_fclose(mmapfile *temp_mmapfile);
char *mmap_fgets(mmapfile *temp_mmapfile);
char *mmap_fgets_multiline(mmapfile * temp_mmapfile);
void strip(char *buffer);
int hashfunc(const char *name1, const char *name2, int hashslots);
int compare_hashdata(const char *val1a, const char *val1b, const char *val2a,
                            const char *val2b);
void get_datetime_string(time_t *raw_time, char *buffer,
                                int buffer_length, int type);
void get_time_breakdown(unsigned long raw_time, int *days, int *hours,
                               int *minutes, int *seconds);

NAGIOS_END_DECL
#endif
