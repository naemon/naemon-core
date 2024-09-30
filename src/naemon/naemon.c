#include "config.h"
#include "common.h"
#include "objects.h"
#include "comments.h"
#include "downtime.h"
#include "statusdata.h"
#include "macros.h"
#include "sretention.h"
#include "perfdata.h"
#include "broker.h"
#include "nebmods.h"
#include "nebmodules.h"
#include "workers.h"
#include "nerd.h"
#include "query-handler.h"
#include "configuration.h"
#include "commands.h"
#include "events.h"
#include "utils.h"
#include "defaults.h"
#include "globals.h"
#include "logging.h"
#include "nm_alloc.h"
#include "checks.h"

#include "worker/worker.h"

#include <getopt.h>
#include <string.h>
#include <fcntl.h>

static int test_path_access(const char *program, int mode)
{
	char *envpath, *p, *colon;
	int ret, our_errno = 1500; /* outside errno range */

	if (program[0] == '/' || !(envpath = getenv("PATH")))
		return access(program, mode);

	if (!(envpath = strdup(envpath))) {
		errno = ENOMEM;
		return -1;
	}

	for (p = envpath; p; p = colon + 1) {
		char *path;

		colon = strchr(p, ':');
		if (colon)
			*colon = 0;
		nm_asprintf(&path, "%s/%s", p, program);
		ret = access(path, mode);
		free(path);
		if (!ret)
			break;

		if (ret < 0) {
			if (errno == ENOENT)
				continue;
			if (our_errno > errno)
				our_errno = errno;
		}
		if (!colon)
			break;
	}

	free(envpath);

	if (!ret)
		errno = 0;
	else
		errno = our_errno;

	return ret;
}

/*
 * only handles logfile for now, which we stash in macros to
 * make sure we can log *somewhere* in case the new path is
 * completely inaccessible.
 */
static int test_configured_paths(void)
{
	FILE *fp;
	nagios_macros *mac;

	mac = get_global_macros();

	fp = fopen(log_file, "a+");
	if (!fp) {
		/*
		 * The variable trashing is so the logging code can
		 * open the old logfile (if any), in case we got a
		 * restart command or a SIGHUP
		 */
		char *value_absolute = log_file;
		log_file = mac->x[MACRO_LOGFILE];
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to open logfile '%s' for writing: %s\n", value_absolute, strerror(errno));
		return ERROR;
	}

	fclose(fp);

	/* save the macro */
	mac->x[MACRO_LOGFILE] = log_file;
	return OK;
}

/* this is the main event handler loop */
static void event_execution_loop(void)
{
	while (!sigshutdown && !sigrestart) {
		if (sigrotate == TRUE) {
			sigrotate = FALSE;
			rotate_log_file(time(NULL));
			update_program_status(FALSE);
		}

		if (event_poll())
			break;
	}
}

int main(int argc, char **argv)
{
	int result;
	int error = FALSE;
	int display_license = FALSE;
	int display_help = FALSE;
	int c = 0;
	int allow_root = FALSE;
	struct tm *tm, tm_s;
	time_t now;
	char datestring[256];
	nagios_macros *mac;
	const char *worker_socket = NULL;
	int i;
	struct kvvec *global_store;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"license", no_argument, 0, 'V'},
		{"verify-config", no_argument, 0, 'v'},
		{"daemon", no_argument, 0, 'd'},
		{"precache-objects", no_argument, 0, 'p'},
		{"use-precached-objects", no_argument, 0, 'u'},
		{"enable-timing-point", no_argument, 0, 'T'},
		{"worker", required_argument, 0, 'W'},
		{"allow-root", no_argument, 0, 'R'},
		{0, 0, 0, 0}
	};
#define getopt(argc, argv, o) getopt_long(argc, argv, o, long_options, &option_index)
#endif

	/* make sure we have the correct number of command line arguments */
	if (argc < 2)
		error = TRUE;

	/* get all command line arguments */
	while (1) {
		c = getopt(argc, argv, "+hVvdspuxTW");

		if (c == -1 || c == EOF)
			break;

		switch (c) {

		case '?': /* usage */
		case 'h':
			display_help = TRUE;
			break;

		case 'V': /* version */
			display_license = TRUE;
			break;

		case 'v': /* verify */
			verify_config++;
			break;

		case 's': /* scheduling check */
			printf("Warning: -s is deprecated and will be removed\n");
			break;

		case 'd': /* daemon mode */
			daemon_mode = TRUE;
			break;

		case 'p': /* precache object config */
			precache_objects = TRUE;
			break;

		case 'u': /* use precached object config */
			use_precached_objects = TRUE;
			break;
		case 'T':
			enable_timing_point = TRUE;
			break;
		case 'W':
			worker_socket = optarg;
			break;
		case 'R':
			allow_root = TRUE;
			break;

		case 'x':
			printf("Warning: -x is deprecated and will be removed\n");
			break;

		default:
			break;
		}

	}

	/* Make all GLib domain messages go to the usual places. This also maps
	 * GLib levels to an approximation of their corresponding Naemon levels
	 * (including debug).
	 *
	 * Note that because of the GLib domain restriction, log messages from
	 * other domains (such as if we did g_message(...) ourselves from inside
	 * Naemon) do not currently go to this handler.
	 **/
	nm_g_log_handler_id = g_log_set_handler("GLib", G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL |
	                                        G_LOG_FLAG_RECURSION, nm_g_log_handler, NULL);
	mac = get_global_macros();

	global_store = get_global_store();
	if (global_store && !kvvec_init(global_store, 0)) {
		exit(ERROR);
	}

	/* if we're a worker we can skip everything below */
	if (worker_socket) {
		exit(nm_core_worker(worker_socket));
	}

	if (daemon_mode == FALSE) {
		printf("\nNaemon Core " VERSION "\n");
		printf("Copyright (c) 2013-present Naemon Core Development Team and Community Contributors\n");
		printf("Copyright (c) 2009-2013 Nagios Core Development Team and Community Contributors\n");
		printf("Copyright (c) 1999-2009 Ethan Galstad\n");
		printf("License: GPL\n\n");
		printf("Website: https://www.naemon.io\n");
	}

	/* just display the license */
	if (display_license == TRUE) {

		printf("This program is free software; you can redistribute it and/or modify\n");
		printf("it under the terms of the GNU General Public License version 2 as\n");
		printf("published by the Free Software Foundation.\n\n");
		printf("This program is distributed in the hope that it will be useful,\n");
		printf("but WITHOUT ANY WARRANTY; without even the implied warranty of\n");
		printf("MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n");
		printf("GNU General Public License for more details.\n\n");
		printf("You should have received a copy of the GNU General Public License\n");
		printf("along with this program; if not, write to the Free Software\n");
		printf("Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n\n");

		exit(OK);
	}

	/* make sure we got the main config file on the command line... */
	if (optind >= argc)
		error = TRUE;

	/* if there are no command line options (or if we encountered an error), print usage */
	if (error == TRUE || display_help == TRUE) {

		printf("Usage: %s [options] <main_config_file>\n", argv[0]);
		printf("\n");
		printf("Options:\n");
		printf("\n");
		printf("  -v, --verify-config          Verify all configuration data (-v -v for more info)\n");
		printf("  -T, --enable-timing-point    Enable timed commentary on initialization\n");
		printf("  -x, --dont-verify-paths      Deprecated (Don't check for circular object paths)\n");
		printf("  -p, --precache-objects       Precache object configuration\n");
		printf("  -u, --use-precached-objects  Use precached object config file\n");
		printf("  -d, --daemon                 Starts Naemon in daemon mode, instead of as a foreground process\n");
		printf("  -W, --worker /path/to/socket Act as a worker for an already running daemon\n");
		printf("  --allow-root                 Let naemon run as root. THIS IS NOT RECOMMENDED AT ALL.\n");
		printf("\n");
		printf("Visit the Naemon website at https://www.naemon.io/ for bug fixes, new\n");
		printf("releases, online documentation, FAQs and more...\n");
		printf("\n");

		exit(ERROR);
	}

	if (getuid() == 0) {
		if (allow_root == FALSE) {
			printf("ERROR: do not start naemon as root user.\n");
			exit(EXIT_FAILURE);
		} else {
			printf("WARNING: you are running as root which is not recommended.\n");
		}
	}



	/*
	 * config file is last argument specified.
	 * Make sure it uses an absolute path
	 */
	config_file = nspath_absolute(argv[optind], NULL);
	if (config_file == NULL) {
		printf("Error allocating memory.\n");
		exit(ERROR);
	}

	config_file_dir = nspath_absolute_dirname(config_file, NULL);

	/*
	 * Set the signal handler for the SIGXFSZ signal here because
	 * we may encounter this signal before the other signal handlers
	 * are set.
	 */
	signal(SIGXFSZ, sighandler);


	/*
	 * Setup rand and srand. Don't bother with better resolution than second
	 */
	srand(time(NULL));

	/*
	 * let's go to town. We'll be noisy if we're verifying config
	 * or running scheduling tests.
	 */
	if (verify_config || precache_objects) {
		reset_variables();

		if (verify_config)
			printf("Reading configuration data...\n");

		/* read our config file */
		result = read_main_config_file(config_file);
		if (result != OK) {
			printf("   Error processing main config file!\n\n");
			exit(EXIT_FAILURE);
		}

		if (verify_config)
			printf("   Read main config file okay...\n");

		/*
		 * this must come after dropping privileges, so we make
		 * sure to test access permissions as the right user.
		 */
		if (test_configured_paths() == ERROR) {
			printf("   One or more path problems detected. Aborting.\n");
			exit(EXIT_FAILURE);
		}

		/* read object config files */
		result = read_all_object_data(config_file);
		if (result != OK) {
			printf("   Error processing object config files!\n\n");
			/* if the config filename looks fishy, warn the user */
			if (!strstr(config_file, "naemon.cfg")) {
				printf("\n***> The name of the main configuration file looks suspicious...\n");
				printf("\n");
				printf("     Make sure you are specifying the name of the MAIN configuration file on\n");
				printf("     the command line and not the name of another configuration file.  The\n");
				printf("     main configuration file is typically '%s'\n", get_default_config_file());
			}

			printf("\n***> One or more problems was encountered while processing the config files...\n");
			printf("\n");
			printf("     Check your configuration file(s) to ensure that they contain valid\n");
			printf("     directives and data definitions.  If you are upgrading from a previous\n");
			printf("     version of Naemon, you should be aware that some variables/definitions\n");
			printf("     may have been removed or modified in this version.  Make sure to read\n");
			printf("     the HTML documentation regarding the config files, as well as the\n");
			printf("     'Whats New' section to find out what has changed.\n\n");
			exit(EXIT_FAILURE);
		}

		if (verify_config) {
			printf("   Read object config files okay...\n\n");
			printf("Running pre-flight check on configuration data...\n\n");
		}

		/* run the pre-flight check to make sure things look okay... */
		result = pre_flight_check();

		if (result != OK) {
			printf("\n***> One or more problems was encountered while running the pre-flight check...\n");
			printf("\n");
			printf("     Check your configuration file(s) to ensure that they contain valid\n");
			printf("     directives and data definitions.  If you are upgrading from a previous\n");
			printf("     version of Naemon, you should be aware that some variables/definitions\n");
			printf("     may have been removed or modified in this version.  Make sure to read\n");
			printf("     the HTML documentation regarding the config files, as well as the\n");
			printf("     'Whats New' section to find out what has changed.\n\n");
			exit(EXIT_FAILURE);
		}

		if (verify_config) {
			printf("\nThings look okay - No serious problems were detected during the pre-flight check\n");
		}

		if (precache_objects) {
			result = fcache_objects(object_precache_file);
			timing_point("Done precaching objects\n");
			if (result == OK) {
				printf("Object precache file created:\n%s\n", object_precache_file);
			} else {
				printf("Failed to precache objects to '%s': %s\n", object_precache_file, strerror(errno));
			}
		}

		/* clean up after ourselves */
		timing_point("Cleaning up\n");
		cleanup();

		/* exit */
		timing_point("Exiting\n");

		/* make valgrind shut up about still reachable memory */
		neb_free_module_list();
		free(config_file_dir);
		free(config_file);

		exit(result);
	}


	/* start to monitor things... */

	/*
	 * if we're called with a relative path we must make
	 * it absolute so we can launch our workers.
	 * If not, we needn't bother, as we're using execvp()
	 */
	if (strchr(argv[0], '/'))
		naemon_binary_path = nspath_absolute(argv[0], NULL);
	else
		naemon_binary_path = nm_strdup(argv[0]);

	if (!(nagios_iobs = iobroker_create())) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to create IO broker set: %s\n",
		       strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* keep monitoring things until we get a shutdown command */
	sigshutdown = sigrestart = FALSE;
	do {
		/* reset internal book-keeping (in case we're restarting) */
		wproc_num_workers_spawned = wproc_num_workers_online = 0;

		/* reset program variables */
		timing_point("Resetting variables\n");
		reset_variables();
		timing_point("Reset variables\n");

		/* get PID */
		nagios_pid = (int)getpid();

		/* read in the configuration files (main and resource config files) */
		timing_point("Reading main config file\n");
		result = read_main_config_file(config_file);
		if (result != OK) {
			nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to process config file '%s'. Aborting\n", config_file);
			exit(EXIT_FAILURE);
		}
		timing_point("Read main config file\n");

		/* NOTE 11/06/07 EG moved to after we read config files, as user may have overridden timezone offset */
		/* get program (re)start time and save as macro */
		program_start = time(NULL);
		nm_free(mac->x[MACRO_PROCESSSTARTTIME]);
		nm_asprintf(&mac->x[MACRO_PROCESSSTARTTIME], "%lu", (unsigned long)program_start);

		if (test_path_access(naemon_binary_path, X_OK)) {
			nm_log(NSLOG_RUNTIME_ERROR, "Error: failed to access() %s: %s\n", naemon_binary_path, strerror(errno));
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Spawning workers will be impossible. Aborting.\n");
			exit(EXIT_FAILURE);
		}

		if (test_configured_paths() == ERROR) {
			/* error has already been logged */
			exit(EXIT_FAILURE);
		}
		/* enter daemon mode (unless we're restarting...) */
		if (daemon_mode == TRUE && sigrestart == FALSE) {

			result = daemon_init();

			/* we had an error daemonizing, so bail... */
			if (result == ERROR) {
				nm_log(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, "Bailing out due to failure to daemonize. (PID=%d)", (int)getpid());
				cleanup();
				exit(EXIT_FAILURE);
			}

			/* get new PID */
			nagios_pid = (int)getpid();
		}

		/* this must be logged after we read config data, as user may have changed location of main log file */
		nm_log(NSLOG_PROCESS_INFO, "Naemon "VERSION" starting... (PID=%d)\n", (int)getpid());

		/* log the local time - may be different than clock time due to timezone offset */
		now = time(NULL);
		tm = localtime_r(&now, &tm_s);
		strftime(datestring, sizeof(datestring), "%a %b %d %H:%M:%S %Z %Y", tm);
		nm_log(NSLOG_PROCESS_INFO, "Local time is %s", datestring);

		/* write log version/info */
		write_log_file_info(NULL);

		/* open debug log now that we're the right user */
		open_debug_log();

		/* initialize modules */
		timing_point("Initializing NEB module API\n");
		neb_init_modules();
		neb_init_callback_list();
		timing_point("Initialized NEB module API\n");

		/* handle signals (interrupts) before we do any socket I/O */
		setup_sighandler();

		/*
		 * Initialize query handler and event subscription service.
		 * This must be done before modules are initialized, so
		 * the modules can use our in-core stuff properly
		 */
		timing_point("Initializing Query handler\n");
		if (qh_init(qh_socket_path) != OK) {
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to initialize query handler. Aborting\n");
			exit(EXIT_FAILURE);
		}
		timing_point("Initialized Query handler\n");

		timing_point("Initializing NERD\n");
		nerd_init();
		timing_point("Initialized NERD\n");

		/* read in all object config data */
		if (result == OK) {
			timing_point("Reading all object data\n");
			result = read_all_object_data(config_file);
			timing_point("Read all object data\n");
		}

		/*
		 * the queue has to be initialized before loading the neb modules
		 * to give them the chance to register user events.
		 * (initializing event queue requires number of objects, so do
		 * this after parsing the objects)
		 */
		timing_point("Initializing Event queue\n");
		init_event_queue();
		timing_point("Initialized Event queue\n");

		registered_commands_init(200);
		register_core_commands();
		/* fire up command file worker */
		timing_point("Launching command file worker\n");
		launch_command_file_worker();
		timing_point("Launched command file worker\n");

		/* initialize check workers */
		timing_point("Spawning %u workers\n", wproc_num_workers_spawned);
		if (init_workers(num_check_workers) < 0) {
			nm_log(NSLOG_RUNTIME_ERROR, "Failed to spawn workers. Aborting\n");
			exit(EXIT_FAILURE);
		}
		timing_point("Spawned %u workers\n", wproc_num_workers_spawned);

		timing_point("Connecting %u workers\n", wproc_num_workers_online);
		i = 0;
		while (i < 50 && wproc_num_workers_online < wproc_num_workers_spawned) {
			iobroker_poll(nagios_iobs, 50);
			i++;
		}
		timing_point("Connected %u workers\n", wproc_num_workers_online);

		/* load modules */
		timing_point("Loading modules\n");
		if (neb_load_all_modules() != OK) {
			nm_log(NSLOG_CONFIG_ERROR, "Error: Module loading failed. Aborting.\n");
			/* give already loaded modules a chance to deinitialize */
			neb_unload_all_modules(NEBMODULE_FORCE_UNLOAD, NEBMODULE_NEB_SHUTDOWN);
			exit(EXIT_FAILURE);
		}
		timing_point("Loaded modules\n");

		/* close stdin after the neb modules loaded so they can still ask for passwords */
		if (daemon_mode == TRUE && sigrestart == FALSE)
			close_standard_fds();

		timing_point("Making first callback\n");
		broker_program_state(NEBTYPE_PROCESS_PRELAUNCH, NEBFLAG_NONE, NEBATTR_NONE);
		timing_point("Made first callback\n");

		/* there was a problem reading the config files */
		if (result != OK) {
			nm_log(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR | NSLOG_CONFIG_ERROR, "Bailing out due to one or more errors encountered in the configuration files. Run Naemon from the command line with the -v option to verify your config before restarting. (PID=%d)", (int)getpid());
		} else {
			/* run the pre-flight check to make sure everything looks okay*/
			timing_point("Running pre flight check\n");
			if ((result = pre_flight_check()) != OK) {
				nm_log(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR | NSLOG_VERIFICATION_ERROR, "Bailing out due to errors encountered while running the pre-flight check.  Run Naemon from the command line with the -v option to verify your config before restarting. (PID=%d)\n", (int)getpid());
			}
			timing_point("Ran pre flight check\n");
		}

		/* an error occurred that prevented us from (re)starting */
		if (result != OK) {

			/* if we were restarting, we need to cleanup from the previous run */
			if (sigrestart == TRUE) {

				/* clean up the status data */
				cleanup_status_data(TRUE);
			}

			broker_program_state(NEBTYPE_PROCESS_SHUTDOWN, NEBFLAG_PROCESS_INITIATED, NEBATTR_SHUTDOWN_ABNORMAL);

			cleanup();
			shutdown_command_file_worker();
			exit(ERROR);
		}

		/* write the objects.cache file */
		timing_point("Caching objects\n");
		fcache_objects(object_cache_file);
		timing_point("Cached objects\n");

		broker_program_state(NEBTYPE_PROCESS_START, NEBFLAG_NONE, NEBATTR_NONE);

		timing_point("Initializing status data\n");
		initialize_status_data(config_file);
		timing_point("Initialized status data\n");

		/* initialize scheduled downtime data */
		timing_point("Initializing downtime data\n");
		initialize_downtime_data();
		timing_point("Initialized downtime data\n");

		/* initialize comment data */
		timing_point("Initializing comment data\n");
		initialize_comment_data();
		timing_point("Initialized comment data\n");

		/* read initial service and host state information  */
		timing_point("Initializing retention data\n");
		initialize_retention_data();
		timing_point("Initialized retention data\n");

		timing_point("Reading initial state information\n");
		read_initial_state_information();
		timing_point("Read initial state information\n");
		timing_point("Restored %d downtimes\n", number_of_downtimes());
		timing_point("Restored %d comments\n", number_of_comments());

		/* initialize performance data */
		timing_point("Initializing performance data\n");
		initialize_performance_data(config_file);
		timing_point("Initialized performance data\n");

		/* initialize the check execution subsystem */
		timing_point("Initializing check execution scheduling\n");
		checks_init();
		timing_point("Initialized check execution scheduling\n");

		/* initialize check statistics */
		timing_point("Initializing check stats\n");
		init_check_stats();
		timing_point("Initialized check stats\n");

		/* update all status data (with retained information) */
		timing_point("Updating status data\n");
		update_all_status_data();
		timing_point("Updated status data\n");

		/* log initial host and service state */
		timing_point("Logging initial states\n");
		log_host_states(INITIAL_STATES, NULL);
		log_service_states(INITIAL_STATES, NULL);
		timing_point("Logged initial states\n");

		broker_program_state(NEBTYPE_PROCESS_EVENTLOOPSTART, NEBFLAG_NONE, NEBATTR_NONE);

		/* get event start time and save as macro */
		event_start = time(NULL);
		nm_free(mac->x[MACRO_EVENTSTARTTIME]);
		nm_asprintf(&mac->x[MACRO_EVENTSTARTTIME], "%lu", (unsigned long)event_start);

		/* let the parent know we're good to go and that it can let go */
		if (daemon_mode == TRUE && sigrestart == FALSE) {
			if ((result = signal_parent(OK)) != OK) {
				broker_program_state(NEBTYPE_PROCESS_SHUTDOWN, NEBFLAG_PROCESS_INITIATED, NEBATTR_SHUTDOWN_ABNORMAL);
				cleanup();
				exit(ERROR);
			}
		}

		timing_point("Entering event execution loop\n");
		/***** start monitoring all services *****/
		/* (doesn't return until a restart or shutdown signal is encountered) */
		sigshutdown = sigrestart = FALSE;
		event_execution_loop();

		/*
		 * immediately deinitialize the query handler so it
		 * can remove modules that have stashed data with it
		 */
		qh_deinit(qh_socket_path);

		/*
		 * handle any incoming signals
		 */
		signal_react();

		broker_program_state(NEBTYPE_PROCESS_EVENTLOOPEND, NEBFLAG_NONE, NEBATTR_NONE);
		if (sigshutdown == TRUE)
			broker_program_state(NEBTYPE_PROCESS_SHUTDOWN, NEBFLAG_USER_INITIATED, NEBATTR_SHUTDOWN_NORMAL);
		else if (sigrestart == TRUE)
			broker_program_state(NEBTYPE_PROCESS_RESTART, NEBFLAG_USER_INITIATED, NEBATTR_RESTART_NORMAL);

		disconnect_command_file_worker();

		/* save service and host state information */
		save_state_information(FALSE);
		cleanup_retention_data();

		/* clean up performance data */
		cleanup_performance_data();

		/* clean up the scheduled downtime data */
		cleanup_downtime_data();

		/* clean up the status data unless we're restarting */
		cleanup_status_data(!sigrestart);

		registered_commands_deinit();
		free_worker_memory(WPROC_FORCE);
		/* shutdown stuff... */
		if (sigshutdown == TRUE) {
			iobroker_destroy(nagios_iobs, IOBROKER_CLOSE_SOCKETS);
			nagios_iobs = NULL;

			/* log a shutdown message */
			nm_log(NSLOG_PROCESS_INFO, "Successfully shutdown... (PID=%d)\n", (int)getpid());
		}

		/* clean up after ourselves */
		cleanup();

		/* close debug log */
		close_debug_log();

	} while (sigrestart == TRUE && sigshutdown == FALSE);

	shutdown_command_file_worker();

	if (daemon_mode == TRUE)
		unlink(lock_file);

	/* free misc memory */
	nm_free(lock_file);
	nm_free(config_file);
	nm_free(config_file_dir);
	nm_free(naemon_binary_path);

	return OK;
}
