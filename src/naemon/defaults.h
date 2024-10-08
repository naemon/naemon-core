#ifndef INCLUDE_defaults_h__
#define INCLUDE_defaults_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "buildopts.h"

/******************* DEFAULT VALUES *******************/

#define DEFAULT_LOG_LEVEL					1	/* log all events to main log file */
#define DEFAULT_USE_SYSLOG					1	/* log events to syslog? 1=yes, 0=no */
#define DEFAULT_SYSLOG_LEVEL					2	/* log only severe events to syslog */

#define DEFAULT_NOTIFICATION_LOGGING				1	/* log notification events? 1=yes, 0=no */
#define DEFAULT_NSR_LOGGING					1	/* log notification suppression reasons? 1=yes, 0=no */
#define DEFAULT_INTER_CHECK_DELAY				5.0	/* seconds between initial service check scheduling */
#define DEFAULT_INTERLEAVE_FACTOR      				1       /* default interleave to use when scheduling checks */
#define DEFAULT_RETRY_INTERVAL  				30	/* services are retried in 30 seconds if they're not OK */
#define DEFAULT_CHECK_REAPER_INTERVAL				10	/* interval in seconds to reap host and service check results */
#define DEFAULT_MAX_REAPER_TIME                 		30      /* maximum number of seconds to spend reaping service checks before we break out for a while */
#define DEFAULT_MAX_CHECK_RESULT_AGE				3600    /* maximum number of seconds that a check result file is considered to be valid */
#define DEFAULT_MAX_PARALLEL_SERVICE_CHECKS 			0	/* maximum number of service checks we can have running at any given time (0=unlimited) */
#define DEFAULT_RETENTION_UPDATE_INTERVAL			60	/* minutes between auto-save of retention data */
#define DEFAULT_RETAINED_SCHEDULING_RANDOMIZE_WINDOW	60	/* number of seconds used for randomizing the re-scheduling of checks missed over a restart */
#define DEFAULT_RETENTION_SCHEDULING_HORIZON    		900     /* max seconds between program restarts that we will preserve scheduling information */
#define DEFAULT_STATUS_UPDATE_INTERVAL				60	/* seconds between aggregated status data updates */
#define DEFAULT_FRESHNESS_CHECK_INTERVAL        		60      /* seconds between service result freshness checks */
#define DEFAULT_ORPHAN_CHECK_INTERVAL           		60      /* seconds between checks for orphaned hosts and services */

#define DEFAULT_INTERVAL_LENGTH  60 /* seconds per interval unit for check scheduling */

#define DEFAULT_NOTIFICATION_TIMEOUT				30	/* max time in seconds to wait for notification commands to complete */
#define DEFAULT_EVENT_HANDLER_TIMEOUT				30	/* max time in seconds to wait for event handler commands to complete */
#define DEFAULT_HOST_CHECK_TIMEOUT				30	/* max time in seconds to wait for host check commands to complete */
#define DEFAULT_SERVICE_CHECK_TIMEOUT				60	/* max time in seconds to wait for service check commands to complete */
#define DEFAULT_OCSP_TIMEOUT					15	/* max time in seconds to wait for obsessive compulsive processing commands to complete */
#define DEFAULT_OCHP_TIMEOUT					15	/* max time in seconds to wait for obsessive compulsive processing commands to complete */
#define DEFAULT_PERFDATA_TIMEOUT                		5       /* max time in seconds to wait for performance data commands to complete */
#define DEFAULT_TIME_CHANGE_THRESHOLD				900	/* compensate for time changes of more than 15 minutes */

#define DEFAULT_LOG_HOST_RETRIES				0	/* don't log host retries */
#define DEFAULT_LOG_SERVICE_RETRIES				0	/* don't log service retries */
#define DEFAULT_LOG_EVENT_HANDLERS				1	/* log event handlers */
#define DEFAULT_LOG_INITIAL_STATES				0	/* don't log initial service and host states */
#define DEFAULT_LOG_CURRENT_STATES				1	/* log current service and host states after rotating log */
#define DEFAULT_LOG_EXTERNAL_COMMANDS				1	/* log external commands */
#define DEFAULT_LOG_PASSIVE_CHECKS				1	/* log passive service checks */
#define DEFAULT_LOG_GLOBAL_NOTIFICATIONS			1	/* log global notifications */

#define DEFAULT_DEBUG_LEVEL                                     0       /* don't log any debugging information */
#define DEFAULT_DEBUG_VERBOSITY                                 1
#define DEFAULT_MAX_DEBUG_FILE_SIZE                             1000000 /* max size of debug log */

#define DEFAULT_AGGRESSIVE_HOST_CHECKING			0	/* don't use "aggressive" host checking */
#define DEFAULT_CHECK_EXTERNAL_COMMANDS				1 	/* check for external commands */
#define DEFAULT_CHECK_ORPHANED_SERVICES				1	/* check for orphaned services */
#define DEFAULT_CHECK_ORPHANED_HOSTS            		1       /* check for orphaned hosts */
#define DEFAULT_ENABLE_FLAP_DETECTION           		0       /* don't enable flap detection */
#define DEFAULT_PROCESS_PERFORMANCE_DATA        		0       /* don't process performance data */
#define DEFAULT_CHECK_SERVICE_FRESHNESS         		1       /* check service result freshness */
#define DEFAULT_CHECK_HOST_FRESHNESS            		0       /* don't check host result freshness */
#define DEFAULT_TRANSLATE_PASSIVE_HOST_CHECKS                   0       /* should we translate DOWN/UNREACHABLE passive host checks? */
#define DEFAULT_PASSIVE_HOST_CHECKS_SOFT                        0       /* passive host checks are treated as HARD by default */

#define DEFAULT_LOW_SERVICE_FLAP_THRESHOLD			20.0	/* low threshold for detection of service flapping */
#define DEFAULT_HIGH_SERVICE_FLAP_THRESHOLD			30.0	/* high threshold for detection of service flapping */
#define DEFAULT_LOW_HOST_FLAP_THRESHOLD				20.0	/* low threshold for detection of host flapping */
#define DEFAULT_HIGH_HOST_FLAP_THRESHOLD			30.0	/* high threshold for detection of host flapping */

#define DEFAULT_CACHED_HOST_CHECK_HORIZON      			15      /* max age in seconds that cached host checks can be used */
#define DEFAULT_CACHED_SERVICE_CHECK_HORIZON    		15      /* max age in seconds that cached service checks can be used */
#define DEFAULT_ENABLE_PREDICTIVE_HOST_DEPENDENCY_CHECKS	1	/* should we use predictive host dependency checks? */
#define DEFAULT_ENABLE_PREDICTIVE_SERVICE_DEPENDENCY_CHECKS	1	/* should we use predictive service dependency checks? */

#define DEFAULT_USE_LARGE_INSTALLATION_TWEAKS                   0       /* don't use tweaks for large Nagios installations */

#define DEFAULT_ADDITIONAL_FRESHNESS_LATENCY			15	/* seconds to be added to freshness thresholds when automatically calculated by Nagios */

#define DEFAULT_CHECK_FOR_UPDATES                               1       /* should we check for new Nagios releases? */
#define DEFAULT_BARE_UPDATE_CHECK                               0       /* report current version and new installs */
#define MINIMUM_UPDATE_CHECK_INTERVAL                           60*60*22 /* 22 hours minimum between checks - please be kind to our servers! */
#define BASE_UPDATE_CHECK_INTERVAL                              60*60*22 /* 22 hours base interval */
#define UPDATE_CHECK_INTERVAL_WOBBLE                            60*60*4  /* 4 hour wobble on top of base interval */
#define BASE_UPDATE_CHECK_RETRY_INTERVAL                        60*60*1  /* 1 hour base retry interval */
#define UPDATE_CHECK_RETRY_INTERVAL_WOBBLE                      60*60*3  /* 3 hour wobble on top of base retry interval */

#define DEFAULT_ALLOW_EMPTY_HOSTGROUP_ASSIGNMENT        2        /* Allow assigning to empty hostgroups by default, but warn about it */
#define DEFAULT_ALLOW_CIRCULAR_DEPENDENCIES             0        /* Allow circular dependencies */
#define DEFAULT_HOST_DOWN_DISABLE_SERVICE_CHECKS        0        /* run service checks if the host is down */
#define DEFAULT_SKIP_CHECK_STATUS                      -1        /* do not change status by default */
#define SKIP_KEEP_RUNNING_WHEN_UP                      -2        /* run service checks as long as the host and service is up (ok/warning) */
#define DEFAULT_SERVICE_PARENTS_DISABLE_SERVICE_CHECKS  0        /* run service checks if service parents are down */

#define DEFAULT_HOST_PERFDATA_FILE_TEMPLATE "[HOSTPERFDATA]\t$TIMET$\t$HOSTNAME$\t$HOSTEXECUTIONTIME$\t$HOSTOUTPUT$\t$HOSTPERFDATA$"
#define DEFAULT_SERVICE_PERFDATA_FILE_TEMPLATE "[SERVICEPERFDATA]\t$TIMET$\t$HOSTNAME$\t$SERVICEDESC$\t$SERVICEEXECUTIONTIME$\t$SERVICELATENCY$\t$SERVICEOUTPUT$\t$SERVICEPERFDATA$"
#define DEFAULT_HOST_PERFDATA_PROCESS_EMPTY_RESULTS 1
#define DEFAULT_SERVICE_PERFDATA_PROCESS_EMPTY_RESULTS 1


/* Legacy way to find out default locations - do not go near these, as they
 * cause configure options to trigger ABI breaks. At some point, they'll become
 * hidden */
#define DEFAULT_TEMP_FILE              NAEMON_LOCALSTATEDIR "/tempfile"
#define DEFAULT_TEMP_PATH              "/tmp"
#define DEFAULT_CHECK_RESULT_PATH      NAEMON_LOCALSTATEDIR "/spool/checkresults"
#define DEFAULT_STATUS_FILE            NAEMON_LOCALSTATEDIR "/status.dat"
#define DEFAULT_LOG_FILE               NAEMON_LOGDIR "/naemon.log"
#define DEFAULT_LOG_ARCHIVE_PATH       NAEMON_LOGDIR "/archives/"
#define DEFAULT_DEBUG_FILE             NAEMON_LOCALSTATEDIR "/naemon.debug"
#define DEFAULT_COMMENT_FILE           NAEMON_LOCALSTATEDIR "/comments.dat" /* not used anywhere */
#define DEFAULT_DOWNTIME_FILE          NAEMON_LOCALSTATEDIR "/downtime.dat" /* not used anywhere */
#define DEFAULT_RETENTION_FILE         NAEMON_LOCALSTATEDIR "/retention.dat"
#define DEFAULT_COMMAND_FILE           NAEMON_LOCALSTATEDIR "/naemon.cmd"
#define DEFAULT_QUERY_SOCKET           NAEMON_LOCALSTATEDIR "/naemon.qh"
#define DEFAULT_CONFIG_FILE			   NAEMON_SYSCONFDIR "/naemon.cfg"
#define DEFAULT_LOCK_FILE              NAEMON_LOCKFILE
#define DEFAULT_OBJECT_CACHE_FILE      NAEMON_LOCALSTATEDIR "/objects.cache"
#define DEFAULT_PRECACHED_OBJECT_FILE  NAEMON_LOCALSTATEDIR "/objects.precache"
#define DEFAULT_EVENT_BROKER_FILE      NAEMON_LOCALSTATEDIR "/broker.socket"

/* modern way to find out default locations */
const char *get_default_temp_file(void);
const char *get_default_temp_path(void);
const char *get_default_check_result_path(void);
const char *get_default_status_file(void);
const char *get_default_log_file(void);
const char *get_default_log_archive_path(void);
const char *get_default_debug_file(void);
const char *get_default_retention_file(void);
const char *get_default_command_file(void);
const char *get_default_query_socket(void);
const char *get_default_config_file(void);
const char *get_default_lock_file(void);
const char *get_default_object_cache_file(void);
const char *get_default_precached_object_file(void);
const char *get_default_event_broker_file(void);

#endif /* INCLUDE_defaults_h__ */
