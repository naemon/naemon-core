#ifndef _NAEMON_H
#define _NAEMON_H

/*
 * NEVER INCLUDE THIS FILE WITHIN A FILE IN THE NAEMON PROJECT
 *
 * ONLY FOR USE FROM BROKER MODULES
 *
 * This file includes everything in naemon, which means, if included in naemon,
 * this will make it impossible to track dependenceies within the naemon project
 * itself. Using this as the entrypoint in broker modules will however make it
 * possible to move methods around within naemon, and still maintain API
 * compatiblity.
 */

#include "broker.h"
#include "checks.h"
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
#include "nagios.h"
#include "nebcallbacks.h"
#include "neberrors.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "nebstructs.h"
#include "nerd.h"
#include "notifications.h"
#include "objects.h"
#include "perfdata.h"
#include "query-handler.h"
#include "sehandlers.h"
#include "shared.h"
#include "sretention.h"
#include "statusdata.h"
#include "utils.h"
#include "workers.h"

/*
 * Defines below is kept pruely of backward compatibility purposes. They aren't
 * used within the naemon project itself.
 *
 * If they should be used within the naemon project, move them to the correct
 * header before use.
 */

/************* MISC LENGTH/SIZE DEFINITIONS ***********/

/*
 NOTE: Plugin length is artificially capped at 8k to prevent runaway plugins from returning MBs/GBs of data
 back to Nagios.  If you increase the 8k cap by modifying this value, make sure you also increase the value
 of MAX_EXTERNAL_COMMAND_LENGTH in common.h to allow for passive checks results received through the external
 command file. EG 10/19/07
 */
#define MAX_PLUGIN_OUTPUT_LENGTH                8192    /* max length of plugin output (including perf data) */

/*********** ROUTE CHECK PROPAGATION TYPES ************/

#define PROPAGATE_TO_PARENT_HOSTS	1
#define PROPAGATE_TO_CHILD_HOSTS	2

/************ SCHEDULED DOWNTIME TYPES ****************/

#define ACTIVE_DOWNTIME                 0       /* active downtime - currently in effect */
#define PENDING_DOWNTIME                1       /* pending downtime - scheduled for the future */

#endif
