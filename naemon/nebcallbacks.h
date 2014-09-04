#ifndef _NEBCALLBACKS_H
#define _NEBCALLBACKS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "nebmodules.h"


/***** CALLBACK TYPES *****/
#define NEBCALLBACK_NUMITEMS                          26    /* total number of callback types we have */

#define NEBCALLBACK_PROCESS_DATA                      0
#define NEBCALLBACK_TIMED_EVENT_DATA                  1
#define NEBCALLBACK_LOG_DATA                          2
#define NEBCALLBACK_SYSTEM_COMMAND_DATA               3
#define NEBCALLBACK_EVENT_HANDLER_DATA                4
#define NEBCALLBACK_NOTIFICATION_DATA                 5
#define NEBCALLBACK_SERVICE_CHECK_DATA                6
#define NEBCALLBACK_HOST_CHECK_DATA                   7
#define NEBCALLBACK_COMMENT_DATA                      8
#define NEBCALLBACK_DOWNTIME_DATA                     9
#define NEBCALLBACK_FLAPPING_DATA                     10
#define NEBCALLBACK_PROGRAM_STATUS_DATA               11
#define NEBCALLBACK_HOST_STATUS_DATA                  12
#define NEBCALLBACK_SERVICE_STATUS_DATA               13
#define NEBCALLBACK_ADAPTIVE_PROGRAM_DATA             14
#define NEBCALLBACK_ADAPTIVE_HOST_DATA                15
#define NEBCALLBACK_ADAPTIVE_SERVICE_DATA             16
#define NEBCALLBACK_EXTERNAL_COMMAND_DATA             17
#define NEBCALLBACK_AGGREGATED_STATUS_DATA            18
#define NEBCALLBACK_RETENTION_DATA                    19
#define NEBCALLBACK_CONTACT_NOTIFICATION_DATA         20
#define NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA  21
#define NEBCALLBACK_ACKNOWLEDGEMENT_DATA              22
#define NEBCALLBACK_STATE_CHANGE_DATA                 23
#define NEBCALLBACK_CONTACT_STATUS_DATA               24
#define NEBCALLBACK_ADAPTIVE_CONTACT_DATA             25

#define nebcallback_flag(x) (1 << (x))

/***** CALLBACK FUNCTIONS *****/
NAGIOS_BEGIN_DECL

int neb_register_callback(int callback_type, void *mod_handle, int priority, int (*callback_func)(int, void *));
int neb_deregister_callback(int callback_type, int (*callback_func)(int, void *));
int neb_deregister_module_callbacks(nebmodule *);

NAGIOS_END_DECL
#endif
