#ifndef _OBJECTS_FCACHE_H
#define _OBJECTS_FCACHE_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <stdio.h>
#include "lib/lnae-utils.h"
#include "nm_writebuf.h"

NAGIOS_BEGIN_DECL

struct command;
struct contact;
struct contactsmember;
struct contactgroup;
struct contactgroupsmember;
struct host;
struct hostdependency;
struct hostescalation;
struct hostgroup;
struct service;
struct servicedependency;
struct serviceescalation;
struct servicegroup;
struct timeperiod;

/*
 * objects.cache writers. fcache_objects() calls these directly; the public
 * fcache_*(FILE *) functions are thin wrappers around them.
 */

/*
 * fprintf() printed a NULL string as "(null)". objects.cache is read back by
 * naemon -p, so keep that byte for byte.
 */
static inline void fc_s(struct nm_writebuf *wb, const char *s)
{
	nm_wb_str(wb, s ? s : "(null)");
}

/* ends a list item: "," if more follow, "\n" after the last one */
static inline void fc_sep(struct nm_writebuf *wb, int more)
{
	nm_wb_mem(wb, more ? "," : "\n", 1);
}

/* printf's %02d */
static inline void fc_02d(struct nm_writebuf *wb, int v)
{
	if (v >= 0 && v < 10)
		nm_wb_lit(wb, "0");
	nm_wb_int(wb, v);
}

/* objects.cache lines are "\tkey\tvalue\n"; key is a string literal */
#define fc_str(wb, key, val)  do { nm_wb_lit((wb), "\t" key "\t"); fc_s((wb), (val)); nm_wb_lit((wb), "\n"); } while (0)
#define fc_int(wb, key, val)  do { nm_wb_lit((wb), "\t" key "\t"); nm_wb_int((wb), (long long)(val)); nm_wb_lit((wb), "\n"); } while (0)
#define fc_uint(wb, key, val) do { nm_wb_lit((wb), "\t" key "\t"); nm_wb_uint((wb), (unsigned long long)(val)); nm_wb_lit((wb), "\n"); } while (0)
#define fc_dbl(wb, key, val)  do { nm_wb_lit((wb), "\t" key "\t"); nm_wb_dbl((wb), "%f", (double)(val)); nm_wb_lit((wb), "\n"); } while (0)

/* For the FILE * wrappers: flush the stream, then write to its descriptor. */
static inline void fc_file_begin(struct nm_writebuf *wb, FILE *fp)
{
	fflush(fp);
	nm_writebuf_init(wb, fileno(fp), 4096);
}

void nm_fcache_customvars(struct nm_writebuf *wb, const struct customvariablesmember *cvlist);
void nm_fcache_command(struct nm_writebuf *wb, const struct command *temp_command);
void nm_fcache_contactlist(struct nm_writebuf *wb, const char *prefix, const struct contactsmember *list);
void nm_fcache_contact(struct nm_writebuf *wb, const struct contact *temp_contact);
void nm_fcache_contactgrouplist(struct nm_writebuf *wb, const char *prefix, const struct contactgroupsmember *list);
void nm_fcache_contactgroup(struct nm_writebuf *wb, const struct contactgroup *temp_contactgroup);
void nm_fcache_host(struct nm_writebuf *wb, const struct host *temp_host);
void nm_fcache_hostdependency(struct nm_writebuf *wb, const struct hostdependency *temp_hostdependency);
void nm_fcache_hostescalation(struct nm_writebuf *wb, const struct hostescalation *temp_hostescalation);
void nm_fcache_hostgroup(struct nm_writebuf *wb, const struct hostgroup *temp_hostgroup);
void nm_fcache_service(struct nm_writebuf *wb, const struct service *temp_service);
void nm_fcache_servicedependency(struct nm_writebuf *wb, const struct servicedependency *temp_servicedependency);
void nm_fcache_serviceescalation(struct nm_writebuf *wb, const struct serviceescalation *temp_serviceescalation);
void nm_fcache_servicegroup(struct nm_writebuf *wb, const struct servicegroup *temp_servicegroup);
void nm_fcache_timeperiod(struct nm_writebuf *wb, const struct timeperiod *temp_timeperiod);

NAGIOS_END_DECL

#endif
