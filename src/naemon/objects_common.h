#ifndef INCLUDE_objects_common_h__
#define INCLUDE_objects_common_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/lnae-utils.h"
#include "common.h"
#include <stdio.h>

NAGIOS_BEGIN_DECL

extern char *illegal_object_chars;

#define MAX_STATE_HISTORY_ENTRIES		21	/* max number of old states to keep track of for flap detection */

/*
 * flags for notification_options, flapping_options and other similar
 * flags. They overlap (hosts and services), so we can't use enum's.
 */
#define OPT_NOTHING       0 /* no options selected */
#define OPT_ALL           (~0) /* everything selected, so all bits set */
#define OPT_DOWN          (1 << STATE_DOWN)
#define OPT_UP            (1 << STATE_UP)
#define OPT_UNREACHABLE   (1 << STATE_UNREACHABLE)
#define OPT_OK            (1 << STATE_OK)
#define OPT_WARNING       (1 << STATE_WARNING)
#define OPT_CRITICAL      (1 << STATE_CRITICAL)
#define OPT_UNKNOWN       (1 << STATE_UNKNOWN)
#define OPT_RECOVERY      OPT_OK
/* and now the "unreal" states... */
#define OPT_PENDING       (1 << 10)
#define OPT_FLAPPING      (1 << 11)
#define OPT_DOWNTIME      (1 << 12)
#define OPT_DISABLED      (1 << 15) /* will denote disabled checks some day */

struct flag_map {
	int opt;
	int ch;
	const char *name;
};

/* macros useful with both hosts and services */
#define flag_set(c, flag)    ((c) |= (flag))
#define flag_get(c, flag)    (unsigned int)((c) & (flag))
#define flag_isset(c, flag)  (flag_get((c), (flag)) == (unsigned int)(flag))
#define flag_unset(c, flag)  (c &= ~(flag))
#define should_stalk(o) flag_isset(o->stalking_options, 1 << o->current_state)
#define should_flap_detect(o) flag_isset(o->flap_detection_options, 1 << o->current_state)
#define should_notify(o) flag_isset(o->notification_options, 1 << o->current_state)
#define add_notified_on(o, f) (o->notified_on |= (1 << f))

typedef struct customvariablesmember {
	char    *variable_name;
	char    *variable_value;
	int     has_been_modified;
	struct customvariablesmember *next;
} customvariablesmember;

struct customvariablesmember *add_custom_variable_to_object(customvariablesmember **, char *, char *);         /* adds a custom variable to an object */

void fcache_customvars(FILE *fp, const struct customvariablesmember *cvlist);

const char *opts2str(int opts, const struct flag_map *map, char ok_char);

const char *state_type_name(int state_type);
const char *check_type_name(int check_type);

int contains_illegal_object_chars(const char *name);

NAGIOS_END_DECL
#endif
