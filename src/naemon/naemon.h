#ifndef _NAEMON_H
#define _NAEMON_H

#if defined (NAEMON_COMPILATION)
#error "Never include naemon/naemon.h within a file in the naemon project - it's for broker modules."
#endif

#define _NAEMON_H_INSIDE

#include "lib/libnaemon.h"
#include "broker.h"
#include "checks.h"
#include "checks_service.h"
#include "checks_host.h"
#include "commands.h"
#include "comments.h"
#include "common.h"
#include "configuration.h"
#include "defaults.h"
#include "downtime.h"
#include "events.h"
#include "flapping.h"
#include "globals.h"
#include "logging.h"
#include "macros.h"
#include "naemon.h"
#include "nebcallbacks.h"
#include "neberrors.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "nebstructs.h"
#include "nerd.h"
#include "notifications.h"
#include "objectlist.h"
#include "objects.h"
#include "objects_command.h"
#include "objects_common.h"
#include "objects_contactgroup.h"
#include "objects_contact.h"
#include "objects_hostdependency.h"
#include "objects_hostescalation.h"
#include "objects_hostgroup.h"
#include "objects_host.h"
#include "objects_servicedependency.h"
#include "objects_serviceescalation.h"
#include "objects_servicegroup.h"
#include "objects_service.h"
#include "objects_timeperiod.h"
#include "perfdata.h"
#include "query-handler.h"
#include "sehandlers.h"
#include "shared.h"
#include "sretention.h"
#include "statusdata.h"
#include "utils.h"
#include "workers.h"

#undef _NAEMON_H_INSIDE

/*
 * Defines below is kept purely of backward compatibility purposes. They aren't
 * used within the naemon project itself.
 *
 * If they should be used within the naemon project, move them to the correct
 * header before use.
 */

/*********** ROUTE CHECK PROPAGATION TYPES ************/

#define PROPAGATE_TO_PARENT_HOSTS	1
#define PROPAGATE_TO_CHILD_HOSTS	2

/************ SCHEDULED DOWNTIME TYPES ****************/

#define ACTIVE_DOWNTIME                 0       /* active downtime - currently in effect */
#define PENDING_DOWNTIME                1       /* pending downtime - scheduled for the future */

#endif
