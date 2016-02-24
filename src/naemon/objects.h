#ifndef _OBJECTS_H
#define _OBJECTS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/lnae-utils.h"

NAGIOS_BEGIN_DECL

/* increment when changes are made to data structures... */
/* Nagios 3 starts at 300, Nagios 4 at 400, etc. */
#define CURRENT_OBJECT_STRUCTURE_VERSION        402

int fcache_objects(char *cache_file);

NAGIOS_END_DECL
#endif
