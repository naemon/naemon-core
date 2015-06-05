#ifndef INCLUDE_objects_command_h__
#define INCLUDE_objects_command_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include "lib/lnae-utils.h"
#include "defaults.h"
#include "objects_common.h"

NAGIOS_BEGIN_DECL

struct command;
typedef struct command command;
struct commandsmember;
typedef struct commandsmember commandsmember;

extern struct command *command_list;
extern struct command **command_ary;

struct command {
	unsigned int id;
	char    *name;
	char    *command_line;
	struct command *next;
};

struct commandsmember {
	char	*command;
	struct command *command_ptr;
	struct	commandsmember *next;
};

int init_objects_command(int elems);
void destroy_objects_command(void);

struct command *create_command(const char *, const char *);
int register_command(command *new_command);
void destroy_command(command *this_command);

struct command *find_bang_command(const char *);
struct command *find_command(const char *);

void fcache_command(FILE *fp, const struct command *temp_command);

NAGIOS_END_DECL
#endif
