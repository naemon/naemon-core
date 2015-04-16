#ifndef INCLUDE_logging_h__
#define INCLUDE_logging_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <glib.h>
#include "lib/lnae-utils.h"

/******************* LOGGING TYPES ********************/

#define NSLOG_RUNTIME_ERROR		1
#define NSLOG_RUNTIME_WARNING		2

#define NSLOG_VERIFICATION_ERROR	4
#define NSLOG_VERIFICATION_WARNING	8

#define NSLOG_CONFIG_ERROR		16
#define NSLOG_CONFIG_WARNING		32

#define NSLOG_PROCESS_INFO		64
#define NSLOG_EVENT_HANDLER		128
/*#define NSLOG_NOTIFICATION		256*/	/* NOT USED ANYMORE - CAN BE REUSED */
#define NSLOG_EXTERNAL_COMMAND		512

#define NSLOG_HOST_UP      		1024
#define NSLOG_HOST_DOWN			2048
#define NSLOG_HOST_UNREACHABLE		4096

#define NSLOG_SERVICE_OK		8192
#define NSLOG_SERVICE_UNKNOWN		16384
#define NSLOG_SERVICE_WARNING		32768
#define NSLOG_SERVICE_CRITICAL		65536

#define NSLOG_PASSIVE_CHECK		131072

#define NSLOG_INFO_MESSAGE		262144

#define NSLOG_HOST_NOTIFICATION		524288
#define NSLOG_SERVICE_NOTIFICATION	1048576

/***************** DEBUGGING LEVELS *******************/

#define DEBUGL_ALL                      -1
#define DEBUGL_NONE                     0
#define DEBUGL_CONFIG			2
#define DEBUGL_PROCESS                  4
#define DEBUGL_STATUSDATA               4
#define DEBUGL_RETENTIONDATA            4
#define DEBUGL_EVENTS                   8
#define DEBUGL_CHECKS                   16
#define DEBUGL_FLAPPING                 16
#define DEBUGL_EVENTHANDLERS            16
#define DEBUGL_PERFDATA                 16
#define DEBUGL_NOTIFICATIONS            32
#define DEBUGL_EVENTBROKER              64
#define DEBUGL_EXTERNALCOMMANDS         128
#define DEBUGL_COMMANDS                 256
#define DEBUGL_DOWNTIME                 512
#define DEBUGL_COMMENTS                 1024
#define DEBUGL_MACROS                   2048
#define DEBUGL_IPC                      4096
#define DEBUGL_SCHEDULING               8192

#define DEBUGV_BASIC                    0
#define DEBUGV_MORE			1
#define DEBUGV_MOST                     2


/******************* STATE LOGGING TYPES **************/

#define INITIAL_STATES                  1
#define CURRENT_STATES                  2


NAGIOS_BEGIN_DECL

extern int log_initial_states;
extern int log_current_states;

/**** Logging Functions ****/
void nm_log(int, const char *, ...)
__attribute__((__format__(__printf__, 2, 3)));
int log_debug_info(int, int, const char *, ...)
__attribute__((__format__(__printf__, 3, 4)));

int rotate_log_file(time_t);			     	/* rotates the main log file */
int write_log_file_info(time_t *); 			/* records log file/version info */
int open_debug_log(void);
int close_debug_log(void);
int close_log_file(void);
int fix_log_file_owner(uid_t uid, gid_t gid);

/* GLib log handler (GLogFunc*) that maps GLib log messages to their
 * corresponding Naemon levels. Only intended for use as a regular handler,
 * don't invoke directly through application code*/
void nm_g_log_handler(const gchar *domain, GLogLevelFlags log_level,
		const gchar *message, gpointer udata);

NAGIOS_END_DECL
#endif
