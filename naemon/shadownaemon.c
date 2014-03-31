/**
 * shadownaemon is a livestatus cache proxy.
 * It shadows a remote naemon core by fetching its current state by livestatus
 * and provides a local (readonly) livestatus socket and therefore removes
 * network latency for slow connections.
 */

#include "shadownaemon.h"
#include <libgen.h>

static int verbose                         = FALSE;
static int daemonmode                      = FALSE;
static int max_number_of_executing_objects = 100;       /* if we have more currently executing objects than this number, we fetch everything. Otherwise the filter query would get too big */
static double short_shadow_update_interval =   3000000; /* refresh every 3 seconds when there are active connections */
static double long_shadow_update_interval  = 120000000; /* refresh every 120 seconds if there haven't been connections for more than 10minutes */
static int should_write_config             = TRUE;
static char *program_version               = NULL;
static int livestatus_mode                 = -1;
static uint64_t last_connection_count      = 0;
static time_t last_connection              = 0;
static time_t last_refresh                 = 0;
static time_t last_program_restart         = 0;
static time_t shadow_program_restart       = 0;
static unsigned long highest_comment_id    = 0;
static unsigned long highest_downtime_id   = 0;
static int input_socket                    = -1;
static int full_refresh_required           = FALSE;
static char *output_folder;
static char *tmp_folder;
static char *archive_folder;
static char *cmds_pattern;
static char *output_socket_path;
static char *resource_config;
static char *objects_file;
static char *livestatus_log;
static char *livestatus_module;
static char *dummy_command;
static const char *self_name;
static const char *input_source;

/* be nice an help people using this tool */
void usage(const char *fmt, ...) {
    printf("Shadownaemon is a livestatus cache proxy.\n");
    printf("It shadows a remote naemon core by fetching its current state by livestatus\n");
    printf("and provides a local (readonly) livestatus socket and therefore removes\n");
    printf("network latency for slow connections.\n");
    printf("Logfiles are not cached and supported, use Thruks Logfile\n");
    printf("Cache for example. Commands must be send to the original site.\n");
    printf("Shadownaemon may accept commands, but will not process them.\n");
    printf("\n");
    printf("Usage: %s -i <input source> -o <output folder>[options]\n", self_name);
    printf("\n");
    printf("Options:\n");
    printf("\n");
    printf("  -h, --help                        Display help and exit.\n");
    printf("  -V, --version                     Display version and exit.\n");
    printf("  -v, --verbose                     Be more verbose.\n");
    printf("  -d, --daemon                      Start in daemon mode.\n");
    printf("\n");
    printf("  -i, --input <connectionstring>    Livestatus input source. (local unix socket, tcp socket\n");
    printf("  -o, --output <folder>             Folder where all runtime data will be stored and the output socket will be created.\n");
    printf("  -l, --livestatus <modulepath>     Path to livestatus module, default: %s\n", get_default_livestatus_module());
    printf("  -r, --refresh <seconds>           Refresh Interval, default: 3 seconds.\n");
    printf("\n");
    printf("\n");
    printf("Example:\n");
    printf("  %%> %s -d -i remotehost:6557 -o /tmp/shadowcache/\n", self_name);
    printf("This will connect to the remote livestatus site on remotehost with port 6556 and\n");
    printf("provides a local livestatus socket in /tmp/shadowcache/live which can be queried.\n");
    printf("\n");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }

    exit(2);
}

/* main action */
int main(int argc, char **argv) {
    int c = 0;
    int option_index;
    char *cwd;
#ifdef HAVE_GETOPT_H
    struct option long_options[] = {
        {"help", no_argument, 0, 'h' },
        {"version", no_argument, 0, 'V' },
        {"verbose", required_argument, 0, 'v' },
        {"daemon", required_argument, 0, 'd' },
        {"input", required_argument, 0, 'i' },
        {"output", required_argument, 0, 'o' },
        {"refresh", required_argument, 0, 'r' },
    };
#define getopt(a, b, c) getopt_long(a, b, c, long_options, &option_index)
#endif

    self_name = strdup(basename(argv[0]));
    shadow_program_restart = time(NULL);

    enable_timing_point = 0;
    for (;;) {
        c = getopt(argc, argv, "hVvdi:r:o:l:");
        if (c < 0 || c == EOF)
            break;

        switch (c) {
            case 'V':
                printf("%s version %s", self_name, PROGRAM_VERSION);
                break;
            case 'h': case '?':
                usage(NULL);
                break;
            case 'v':
                verbose = TRUE;
                enable_timing_point = 1;
                break;
            case 'd':
                daemonmode = TRUE;
                break;
            case 'i':
                input_source = optarg;
                break;
            case 'o':
                output_folder = optarg;
                break;
            case 'l':
                livestatus_module = optarg;
                break;
            case 'r':
                short_shadow_update_interval = atof(optarg) * 1000000;
                if(short_shadow_update_interval <= 0) {
                    usage("refresh interval must be a positive number\n");
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                usage("Unknown argument\n");
                exit(EXIT_FAILURE);
        }
    }

    /* make sure we have all required information */
    if(input_source == NULL)
        usage("ERROR: input source (-i) missing\n\n");

    if(output_folder == NULL)
        usage("ERROR: output folder missing\n\n");

    if(livestatus_module == NULL)
        livestatus_module = get_default_livestatus_module();
    if(livestatus_module == NULL)
        usage("ERROR: livestatus module missing\n\n");

    if(output_folder[(strlen(output_folder)-1)] == '/') {
        output_folder[(strlen(output_folder)-1)] = '\0';
    }

    /* required before daemonizing, because we need full pid path */
    my_free(output_socket_path);
    output_socket_path = malloc(sizeof(char) * 250);
    snprintf(output_socket_path, 249, "%s/%s", output_folder, "live");
    output_folder   = nspath_absolute_dirname(output_socket_path, NULL);
    config_file_dir = nspath_absolute_dirname(output_socket_path, NULL);
    my_free(tmp_folder);
    tmp_folder = malloc(sizeof(char) * 250);
    snprintf(tmp_folder, 249, "%s/%s", output_folder, "tmp");
    nspath_mkdir_p(output_folder, 0700, 0);
    nspath_mkdir_p(tmp_folder, 0700, 0);
    my_free(log_file);
    log_file = malloc(sizeof(char) * 250);
    snprintf(log_file, 249, "%s/%s", tmp_folder, "shadownaemon.log");

    if(daemonmode == TRUE) {
        lock_file = malloc(sizeof(char) * 250);
        snprintf(lock_file, 249, "%s/shadownaemon.pid", tmp_folder);
        // daemon init changes to wrong folder otherwise
#ifdef HAVE_GET_CURRENT_DIR_NAME
        cwd = get_current_dir_name();
#else
        // Emulate get_current_dir_name() without relying on
        // getcwd(NULL, 0) to be working
        {
            size_t size = 50;
            errno = 0;
            do {
                cwd = malloc(size);
                if (!cwd) {
                    goto error_out;
                }
                if (getcwd(cwd, size) == cwd) {
                    break;
                }
                if (errno != ERANGE) {
                    free(cwd);
                    goto error_out;
                }
                size *= 2;
            } while (1);
        }
#endif
        setenv("HOME", cwd, 1);
        free(cwd);
        daemon_dumps_core = TRUE;
        if (daemon_init() == ERROR) {
#ifndef HAVE_GET_CURRENT_DIR_NAME
error_out:
#endif
            /* we had an error daemonizing, so bail... */
            logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "Bailing out due to failure to daemonize. (PID=%d)", (int)getpid());
            cleanup();
            exit(EXIT_FAILURE);
        }
    }

    /* determine livestatus mode */
    if(strstr(input_source, "http:") != NULL) {
        livestatus_mode = LIVESTATUS_MODE_HTTP;
    }
    else if(strstr(input_source, "https:") != NULL) {
        livestatus_mode = LIVESTATUS_MODE_HTTP;
    }
    else if(strstr(input_source, ":") != NULL) {
        livestatus_mode = LIVESTATUS_MODE_TCP;
    }
    else {
        livestatus_mode = LIVESTATUS_MODE_SOCKET;
    }

    /* handle signals (interrupts) before we do any socket I/O */
    setup_sighandler();
    signal(SIGINT, sighandler);

    while(sigshutdown == FALSE) {
        sigrestart           = FALSE;
        last_program_restart = 0;
        if(write_config_files() != OK) {
            logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "remote site not available, waiting 30seconds");
            cleanup();
            sleep(30);
            continue;
        }
        run_refresh_loop();
        if(sigshutdown == FALSE && sigrestart == FALSE) {
            logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "remote site went offline, waiting 30seconds");
            sleep(30);
        }
    }

    /* clean up */
    clean_output_folder();

    /* free some locations */
    my_free(config_file);
    my_free(tmp_folder);
    my_free(log_file);
    my_free(cmds_pattern);
    my_free(output_socket_path);
    my_free(resource_config);
    my_free(objects_file);
    my_free(retention_file);
    my_free(archive_folder);
    my_free(livestatus_log);
    my_free(check_result_path);

    unlink(lock_file);
    my_free(lock_file);

    /* exit */
    return EXIT_SUCCESS;
}

/* return path to default livestatus */
char *get_default_livestatus_module() {
    char *livestatus_path = malloc(sizeof(char) * 250);
    struct stat st;

    snprintf(livestatus_path, 249, "%s/lib/naemon/livestatus.o", getenv("HOME"));
    if(stat(livestatus_path, &st) == 0) {
        return(livestatus_path);
    }

    snprintf(livestatus_path, 249, "/usr/lib/naemon/livestatus.o");
    if(stat(livestatus_path, &st) == 0) {
        return(livestatus_path);
    }

    snprintf(livestatus_path, 249, "/usr/lib64/naemon/livestatus.o");
    if(stat(livestatus_path, &st) == 0) {
        return(livestatus_path);
    }
    my_free(livestatus_path);
    return(NULL);
}

/* remove all files from output folder */
int clean_output_folder() {
    unlink(output_socket_path);
    unlink(config_file);
    unlink(log_file);
    unlink(resource_config);
    unlink(retention_file);
    unlink(livestatus_log);
    unlink(objects_file);
    timing_point("output folder cleaned\n");
    return(OK);
}

/* write all files required to start the core engine */
int write_config_files() {
    FILE *file;
    char *homedir  = getenv("HOME");
    char *omd_root = getenv("OMD_ROOT");
    char *omd_site = getenv("OMD_SITE");

    reset_variables();

    /* set our file locations */
    config_file_dir = nspath_absolute_dirname(output_socket_path, NULL);
    my_free(config_file);
    config_file = malloc(sizeof(char) * 250);
    snprintf(config_file, 249, "%s/%s", tmp_folder, "naemon.cfg");
    my_free(resource_config);
    resource_config = malloc(sizeof(char) * 250);
    snprintf(resource_config, 249, "%s/%s", tmp_folder, "resource.cfg");
    my_free(objects_file);
    objects_file = malloc(sizeof(char) * 250);
    snprintf(objects_file, 249, "%s/%s", tmp_folder, "objects.cfg");
    my_free(retention_file);
    retention_file = malloc(sizeof(char) * 250);
    snprintf(retention_file, 249, "%s/%s", tmp_folder, "retention.dat");
    my_free(livestatus_log);
    livestatus_log = malloc(sizeof(char) * 250);
    snprintf(livestatus_log, 249, "%s/%s", tmp_folder, "livestatus.log");
    my_free(check_result_path);
    check_result_path = strdup(tmp_folder);
    my_free(log_file);
    log_file = malloc(sizeof(char) * 250);
    snprintf(log_file, 249, "%s/%s", tmp_folder, "shadownaemon.log");
    my_free(cmds_pattern);
    cmds_pattern = malloc(sizeof(char) * 250);
    snprintf(cmds_pattern, 249, "%s/*.cmds", tmp_folder);
    my_free(archive_folder);
    archive_folder = malloc(sizeof(char) * 250);
    snprintf(archive_folder, 249, "%s/%s", tmp_folder, "archives");

    if(should_write_config == FALSE) {
        should_write_config = TRUE;
        return(OK);
    }

    /* never log anything anywhere */
    use_syslog = FALSE;

    /* write out files */
    timing_point("writing configuration files\n");
    nspath_mkdir_p(output_folder, 0700, 0);
    if(verbose)
        logit(NSLOG_PROCESS_INFO, TRUE, "writing configuration into: %s\n", output_folder);
    nspath_mkdir_p(tmp_folder, 0700, 0);
    nspath_mkdir_p(archive_folder, 0700, 0);

    /* write minimal naemon.cfg */
    file = fopen(config_file, "w+");
    if(file == NULL) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "cannot write %s: %s\n", config_file, strerror(errno));
        exit(EXIT_FAILURE);
    }
    fprintf(file,"lock_file=%s/shadownaemon.pid\n", tmp_folder);
    fprintf(file,"temp_file=%s/tmp.file\n", tmp_folder);
    fprintf(file,"temp_path=%s\n", tmp_folder);
    fprintf(file,"log_archive_path=%s\n", archive_folder);
    fprintf(file,"check_result_path=%s\n", check_result_path);
    fprintf(file,"state_retention_file=%s\n", retention_file);
    fprintf(file,"debug_file=%s/debug.log\n", tmp_folder);
    fprintf(file,"command_file=%s/naemon.cmd\n", tmp_folder);
    fprintf(file,"log_file=%s\n", log_file);
    fprintf(file,"object_cache_file=%s/objects.cache\n", tmp_folder);
    fprintf(file,"precached_object_file=%s/objects.precache\n", tmp_folder);
    fprintf(file,"resource_file=%s\n", resource_config);
    fprintf(file,"status_file=/dev/null\n");
    fprintf(file,"cfg_file=%s\n", objects_file);
    fprintf(file,"illegal_macro_output_chars=`~$&|'\"<>\n");
    fprintf(file,"event_broker_options=-1\n");
    fprintf(file,"broker_module=%s num_client_threads=20 debug=0 %s\n", livestatus_module, output_socket_path);
    fclose(file);
    timing_point("wrote %s\n", config_file);

    /* write resource.cfg */
    file = fopen(resource_config, "w+");
    if(file == NULL) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "cannot write %s: %s\n", resource_config, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(omd_site == NULL) {
        fprintf(file,"$USER1$=%s\n", homedir);
    } else {
        fprintf(file,"$USER1$=%s/lib/nagios/plugins\n", omd_root);
        fprintf(file,"$USER2$=%s/local/lib/nagios/plugins\n", omd_root);
        fprintf(file,"$USER3$=%s\n", omd_site);
        fprintf(file,"$USER4$=%s\n", omd_root);
    }
    fclose(file);
    timing_point("wrote %s\n", resource_config);

    /* write objects */
    file = fopen(objects_file, "w+");
    if(file == NULL) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "cannot write %s: %s\n", objects_file, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(write_commands_configuration(file) != OK) {
        fclose(file);
        return(ERROR);
    }
    write_timeperiods_configuration(file);
    write_contactgroups_configuration(file);
    write_hostgroups_configuration(file);
    write_servicegroups_configuration(file);
    write_contacts_configuration(file);
    write_hosts_configuration(file);
    write_services_configuration(file);
    fclose(file);
    timing_point("wrote %s\n", objects_file);

    timing_point("wrote configuration files...\n");

    return(OK);
}

/* pretend to be a normal core and read objects and states */
int initialize_core() {
    int result, warnings = 0, errors = 0;
    void (*open_logfile)(void);

    read_main_config_file(config_file);

    /* read object config files */
    result = read_all_object_data(config_file);
    if (result != OK) {
        printf("Error processing object config files. Bailing out\n");
        exit(EXIT_FAILURE);
    }

    timing_point("Config data read\n");

    /* run object pre-flight checks only */
    if (pre_flight_object_check(&warnings, &errors) != OK) {
        printf("Pre-flight check failed. Bailing out\n");
        exit(EXIT_FAILURE);
    }
    timing_point("pre_flight_object_check ready\n");
    if (pre_flight_circular_check(&warnings, &errors) != OK) {
        printf("Pre-flight circular check failed. Bailing out\n");
        exit(EXIT_FAILURE);
    }
    timing_point("pre_flight_circular_check ready\n");


    /* initialize modules */
    neb_init_modules();
    neb_init_callback_list();

    /* there was a problem reading the config files */
    if (result != OK) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR | NSLOG_CONFIG_ERROR, TRUE, "Bailing out due to one or more errors encountered in the configuration files. Run Naemon from the command line with the -v option to verify your config before restarting. (PID=%d)", (int)getpid());
        exit(EXIT_FAILURE);
    }

    init_event_queue();
    timing_point("Event queue initialized\n");

    /* let livestatus module reopen its logfile */
    *(void**)(&open_logfile) = dlsym(RTLD_DEFAULT, "open_logfile");
    if(open_logfile != NULL)
        (void)open_logfile();

    /* load modules */
    if(verbose == FALSE)
        daemon_mode = TRUE; // prevents nebmods from loging to console
    if (neb_load_all_modules() != OK) {
        logit(NSLOG_CONFIG_ERROR, ERROR, "Error: Module loading failed. Aborting.\n");
        exit(EXIT_FAILURE);
    }
    timing_point("Modules loaded\n");

    /* send program data to broker */
    broker_program_state(NEBTYPE_PROCESS_PRELAUNCH, NEBFLAG_NONE, NEBATTR_NONE, NULL);
    timing_point("First callback made\n");

    /* run the pre-flight check to make sure everything looks okay*/
    if(verbose == TRUE) {
        if((result = pre_flight_check()) != OK) {
            logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR | NSLOG_VERIFICATION_ERROR, TRUE, "Bailing out due to errors encountered while running the pre-flight check.  Run Naemon from the command line with the -v option to verify your config before restarting. (PID=%d)\n", (int)getpid());
            exit(EXIT_FAILURE);
        }
        timing_point("Object configuration parsed and understood\n");
    }

    /* send program data to broker */
    broker_program_state(NEBTYPE_PROCESS_START, NEBFLAG_NONE, NEBATTR_NONE, NULL);

    /* initialize scheduled downtime data */
    initialize_downtime_data();
    timing_point("Downtime data initialized\n");

    /* read initial service and host state information  */
    initialize_retention_data(config_file);
    timing_point("Retention data initialized\n");
    read_initial_state_information();
    timing_point("Initial state information read\n");

    /* initialize comment data */
    initialize_comment_data();
    timing_point("Comment data initialized\n");

    /* initialize performance data */
    initialize_performance_data(config_file);
    timing_point("Performance data initialized\n");

    /* initialize the event timing loop */
    init_timing_loop();
    timing_point("Event timing loop initialized\n");

    /* initialize check statistics */
    init_check_stats();
    timing_point("check stats initialized\n");

    /* update all status data (with retained information) */
    update_all_status_data();
    timing_point("Status data updated\n");

    /* make core commands available*/
    registered_commands_init(200);
    register_core_commands();

    return OK;
}

/* cleanup memory and files */
int deinitialize_core() {
    int i;

/* TODO: understand why cleanup segfaults because of empty macros */
    for (i = 0; i < MAX_USER_MACROS; i++) {
        macro_user[i] = NULL;
    }

    /* remove core commands */
    registered_commands_deinit();

    /* send program data to broker */
    broker_program_state(NEBTYPE_PROCESS_EVENTLOOPEND, NEBFLAG_NONE, NEBATTR_NONE, NULL);
    if(sigrestart) {
        broker_program_state(NEBTYPE_PROCESS_RESTART, NEBFLAG_USER_INITIATED, NEBATTR_RESTART_NORMAL, NULL);
    } else {
        broker_program_state(NEBTYPE_PROCESS_SHUTDOWN, NEBFLAG_USER_INITIATED, NEBATTR_SHUTDOWN_NORMAL, NULL);
    }

    cleanup_retention_data();

    /* clean up performance data */
    cleanup_performance_data();

    /* clean up the scheduled downtime data */
    cleanup_downtime_data();

    /* clean up the status data unless we're restarting */
    cleanup_status_data(TRUE);

    /* clean up after ourselves */
    test_scheduling = FALSE;
    verify_config   = FALSE;
    cleanup();

    /* free misc memory */
    my_free(config_file_dir);
    my_free(naemon_binary_path);

    return OK;
}

/* do a livestatus query depending on the input source */
int livestatus_query(result_list **answer, char *source, char *query, char *columns[], int columnssize) {
    int result;
    switch(livestatus_mode) {
        case LIVESTATUS_MODE_SOCKET:
            result = livestatus_query_socket(answer, source, query, columns, columnssize);
            break;
        case LIVESTATUS_MODE_TCP:
            result = livestatus_query_socket(answer, source, query, columns, columnssize);
            break;
        case LIVESTATUS_MODE_HTTP:
            /* not implemented yet */
        default:
            logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "no input method available for: %s\n", source);
            exit(EXIT_FAILURE);
            break;
    }
    return result;
}

/* fetch result from a local socket, return linked list with result */
int livestatus_query_socket(result_list **result, char *socket_path, char *query, char *columns[], int columnssize) {
    int x, columnslength, return_code, result_size, size, row_size, total_read;
    char buffer[14];
    char header[17];
    char *columnsheader = NULL;
    char *ptr;
    char *result_string, *result_string_c, *cell;
    result_list *curr;
    char *send_header = "ResponseHeader: fixed16\nKeepAlive: on\nSeparators: 1 2 5 6\n\n"; /* dataset sep, column sep, list sep, host/svc list sep */

    row_size=0;
    curr = *result;
    curr->set  = NULL;
    curr->next = NULL;

    if(input_socket == -1) {
        if(livestatus_mode == LIVESTATUS_MODE_SOCKET) {
            input_socket = open_local_socket(socket_path);
        } else {
            input_socket = open_tcp_socket(socket_path);
        }
        /* still no connection? */
        if(input_socket == -1) {
            return(-1);
        }
    }

    size = send(input_socket, query, strlen(query), 0);
    if( size <= 0) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "sending to socket failed : %s\n", strerror(errno));
        close(input_socket);
        input_socket = -1;
        return(-1);
    }
    if(query[strlen(query)-1] != '\n') {
        send(input_socket, "\n", 1, 0);
    }
    columnslength = 0;
    for(x=0; x<columnssize; x++)
        columnslength += strlen(columns[x]);
    columnslength += 20 + columnssize;
    columnsheader = malloc(sizeof(char) * columnslength);
    columnsheader[0] = '\0';
    strcat(columnsheader, "Columns: ");
    for(x=0; x<columnssize; x++) {
        strcat(columnsheader, columns[x]);
        if(x != columnssize)
            strcat(columnsheader, " ");
    }
    strcat(columnsheader, "\n");
    size = send(input_socket, columnsheader, strlen(columnsheader), 0);
    size = send(input_socket, send_header, strlen(send_header), 0);
    my_free(columnsheader);
    size = read(input_socket, header, 16);
    if( size < 16) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "reading socket failed (%d bytes read): %s\n", size, strerror(errno));
        if(size > 0)
            logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "got header: '%s'\n", header);
        close(input_socket);
        input_socket = -1;
        return(-1);
    }
    header[size] = '\0';
    strncpy(buffer, header, 3);
    buffer[3] = '\0';
    return_code = atoi(buffer);
    if( return_code != 200) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "query failed: %d\nquery:\n---\n%s\n---\n", return_code, query);
        close(input_socket);
        input_socket = -1;
        return(-1);
    }

    strncpy(buffer, header+3, 13);
    result_size = atoi(buffer);
    if(result_size == 0) {
        return(row_size);
    }

    result_string   = malloc(sizeof(char*)*result_size+1);
    result_string_c = result_string;
    total_read      = 0;
    size            = 0;
    while(total_read < result_size && size >= 0) {
        size = read(input_socket, result_string+total_read, (result_size - total_read));
        total_read += size;
    }
    if( size <= 0 || total_read != result_size) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "reading socket failed (%d bytes read, expected %d): %s\n", total_read, result_size, strerror(errno));
        my_free(result_string_c);
        close(input_socket);
        input_socket = -1;
        return(-1);
    }
    result_string[total_read] = '\0';

    // split result in arrays of arrays
    while((ptr = strsep( &result_string, "\x1")) != NULL) {
        if(!strcmp(ptr, "")) break;
        if(row_size > 0) {
            curr->next = malloc(sizeof(result_list));
            curr = curr->next;
            curr->next = NULL;
        }
        curr->set  = malloc(columnssize*sizeof(char*));
        for(x=0;x<columnssize;x++) {
            cell = strsep( &ptr, "\x2");
            curr->set[x] = strdup(cell);
        }
        row_size++;
    }
    my_free(result_string_c);

    return(row_size);
}

/* cleanup result set */
void free_livestatus_result(result_list * result, int datasize) {
    int x;
    result_list * curr = result;
    while(curr != NULL) {
        result = curr;
        curr   = curr->next;
        if(result->set != NULL) {
            for(x=0;x<datasize;x++) {
                my_free(result->set[x]);
            }
            my_free(result->set);
        }
        my_free(result);
    }
}

/* open local socket connection */
int open_local_socket(char *socket_path) {
    struct sockaddr_un address;
    struct stat st;
    struct timeval tv;
    tv.tv_sec  = 5;  /* 5 Secs Timeout */
    tv.tv_usec = 0;

    if (0 != stat(socket_path, &st)) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "no unix socket %s existing\n", socket_path);
        return(-1);
    }

    if((input_socket=socket (PF_LOCAL, SOCK_STREAM, 0)) <= 0) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "creating socket failed: %s\n", strerror(errno));
        return(-1);
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_LOCAL;
    strcpy(address.sun_path, socket_path);
    setsockopt(input_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(input_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if(!connect(input_socket, (struct sockaddr *) &address, sizeof (address)) == 0) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "connecting socket failed: %s\n", strerror(errno));
        return(-1);
    }
    return(input_socket);
}

/* open tcp network connection */
int open_tcp_socket(char *connection_string) {
    char * server, * server_c, * hostname, * port_val;
    struct sockaddr_in serveraddr;
    in_port_t port;
    struct hostent *hostp;
    struct timeval tv;
    tv.tv_sec  = 30;  /* 30 Secs Timeout */
    tv.tv_usec = 0;

    server   = strdup(connection_string);
    server_c = server;
    hostname = strsep(&server, ":");
    port_val = strsep(&server, "\x0");
    port     = (in_port_t) atoi(port_val);

    if((input_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "creating socket failed: %s\n", strerror(errno));
        return(-1);
    }

    hostp = gethostbyname(hostname);
    if(hostp == (struct hostent *)NULL) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "host %s not found: %s\n", hostname, hstrerror(h_errno));
        my_free(server_c);
        close(input_socket);
        exit(EXIT_FAILURE);
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_port        = htons(port);
    bcopy((char *) hostp->h_addr,(char *)&serveraddr.sin_addr.s_addr,hostp->h_length);

    setsockopt(input_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(input_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if(!connect(input_socket, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) == 0) {
        logit(NSLOG_PROCESS_INFO | NSLOG_RUNTIME_ERROR, TRUE, "connecting to %s:%d failed: %s\n", hostname, port, strerror(errno));
        my_free(server_c);
        return(-1);
    }
    my_free(server_c);
    return(input_socket);
}

/* updates program status based on remote sites data */
int update_program_status_data() {
    int num;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET status";
    char *columns[] = {"accept_passive_host_checks",        // 0
                       "accept_passive_service_checks",
                       "check_external_commands",
                       "check_host_freshness",
                       "check_service_freshness",
                       "enable_event_handlers",             // 5
                       "enable_flap_detection",
                       "enable_notifications",
                       "execute_host_checks",
                       "execute_service_checks",
                       "last_log_rotation",                 // 10
                       "nagios_pid",
                       "obsess_over_hosts",
                       "obsess_over_services",
                       "process_performance_data",
                       "program_start",                     // 15
                       "program_version",
                       "interval_length",
                       "connections",
                       "connections_rate",
                       "external_commands",                 // 20
                       "external_commands_rate",
                       "forks",
                       "forks_rate",
                       "host_checks",
                       "host_checks_rate",                  // 25
                       "neb_callbacks",
                       "neb_callbacks_rate",
                       "requests",
                       "requests_rate",
                       "service_checks",                    // 30
                       "service_checks_rate",
                       "log_messages",
                       "log_messages_rate",
                       "cached_log_messages",
                       "last_command_check",                // 35
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    uint64_t (*g_counters)[NUM_COUNTERS];
    uint64_t (*g_last_counter)[NUM_COUNTERS];
    double (*g_counter_rate)[NUM_COUNTERS];
    int *num_cached_log_messages;
    int *last_command_check;

    *(void**)(&g_counters)              = dlsym(RTLD_DEFAULT, "g_counters");
    *(void**)(&g_last_counter)          = dlsym(RTLD_DEFAULT, "g_last_counter");
    *(void**)(&g_counter_rate)          = dlsym(RTLD_DEFAULT, "g_counter_rate");
    *(void**)(&num_cached_log_messages) = dlsym(RTLD_DEFAULT, "num_cached_log_messages");
    *(void**)(&last_command_check)      = dlsym(RTLD_DEFAULT, "last_command_check");

    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num == 1) {
        if(last_program_restart != 0 && last_program_restart != program_start) {
            sigrestart = TRUE;
            logit(NSLOG_INFO_MESSAGE, TRUE, "remote site has restarted, need new config...\n");
        } else {
            last_program_restart = program_start;

            /* update time of last connection if we had one */
            if(last_connection_count != 0 && (*g_counters)[COUNTER_CONNECTIONS] != 0 && last_connection_count != (*g_counters)[COUNTER_CONNECTIONS]) {
                printf("had %d connections since last refresh\n", (int)((*g_counters)[COUNTER_CONNECTIONS]-last_connection_count));
                last_connection = time(NULL);
            }

            accept_passive_host_checks      = atoi(answer->set[0]);
            accept_passive_service_checks   = atoi(answer->set[1]);
            check_external_commands         = atoi(answer->set[2]);
            check_host_freshness            = atoi(answer->set[3]);
            check_service_freshness         = atoi(answer->set[4]);
            enable_event_handlers           = atoi(answer->set[5]);
            enable_flap_detection           = atoi(answer->set[6]);
            enable_notifications            = atoi(answer->set[7]);
            execute_host_checks             = atoi(answer->set[8]);
            execute_service_checks          = atoi(answer->set[9]);
            last_log_rotation               = atoi(answer->set[10]);
            nagios_pid                      = atoi(answer->set[11]);
            obsess_over_hosts               = atoi(answer->set[12]);
            obsess_over_services            = atoi(answer->set[13]);
            process_performance_data        = atoi(answer->set[14]);
            program_start                   = atoi(answer->set[15]);
            if(program_version == NULL)
                program_version             = strdup(answer->set[16]);
            interval_length                 = atoi(answer->set[17]);

            /* update livestatus counter */
            (*g_counters)[COUNTER_SERVICE_CHECKS] = (uint64_t)atoll(answer->set[30]);
            (*g_counters)[COUNTER_HOST_CHECKS]    = (uint64_t)atoll(answer->set[24]);
            (*g_counters)[COUNTER_NEB_CALLBACKS]  = (uint64_t)atoll(answer->set[26]);
            (*g_counters)[COUNTER_REQUESTS]       = (uint64_t)atoll(answer->set[28]);
            (*g_counters)[COUNTER_CONNECTIONS]    = (uint64_t)atoll(answer->set[18]);
            (*g_counters)[COUNTER_FORKS]          = (uint64_t)atoll(answer->set[22]);
            (*g_counters)[COUNTER_COMMANDS]       = (uint64_t)atoll(answer->set[20]);
            (*g_counters)[COUNTER_LOG_MESSAGES]   = (uint64_t)atoll(answer->set[32]);
            last_connection_count = (*g_counters)[COUNTER_CONNECTIONS];


            (*g_last_counter)[COUNTER_SERVICE_CHECKS] = (uint64_t)atoll(answer->set[30]);
            (*g_last_counter)[COUNTER_HOST_CHECKS]    = (uint64_t)atoll(answer->set[24]);
            (*g_last_counter)[COUNTER_NEB_CALLBACKS]  = (uint64_t)atoll(answer->set[26]);
            (*g_last_counter)[COUNTER_REQUESTS]       = (uint64_t)atoll(answer->set[28]);
            (*g_last_counter)[COUNTER_CONNECTIONS]    = (uint64_t)atoll(answer->set[18]);
            (*g_last_counter)[COUNTER_FORKS]          = (uint64_t)atoll(answer->set[22]);
            (*g_last_counter)[COUNTER_COMMANDS]       = (uint64_t)atoll(answer->set[20]);
            (*g_last_counter)[COUNTER_LOG_MESSAGES]   = (uint64_t)atoll(answer->set[32]);

            (*g_counter_rate)[COUNTER_HOST_CHECKS]    = (double)atof(answer->set[25]);
            (*g_counter_rate)[COUNTER_SERVICE_CHECKS] = (double)atof(answer->set[31]);
            (*g_counter_rate)[COUNTER_NEB_CALLBACKS]  = (double)atof(answer->set[27]);
            (*g_counter_rate)[COUNTER_REQUESTS]       = (double)atof(answer->set[29]);
            (*g_counter_rate)[COUNTER_CONNECTIONS]    = (double)atof(answer->set[19]);
            (*g_counter_rate)[COUNTER_FORKS]          = (double)atof(answer->set[23]);
            (*g_counter_rate)[COUNTER_COMMANDS]       = (double)atof(answer->set[21]);
            (*g_counter_rate)[COUNTER_LOG_MESSAGES]   = (double)atof(answer->set[33]);

            *num_cached_log_messages = (uint64_t)atoll(answer->set[34]);
            *last_command_check      = (uint64_t)atoll(answer->set[35]);

            /* send broker event to make wait headers work */
            broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, MODATTR_NONE, MODATTR_NONE, MODATTR_NONE, NULL);
        }
    } else {
        logit(NSLOG_INFO_MESSAGE, TRUE, "updating program status failed\n");
        return(ERROR);
    }
    free_livestatus_result(answer, columns_size);
    timing_point("updated program status\n");
    return(OK);
}

/* updates host status based on remote sites data */
int update_host_status_data() {
    int num, running, len;
    host *hst = NULL;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query  = "GET hosts";
    char *filtered_query;
    char *columns[] = {"name",                          // 0
                       "accept_passive_checks",
                       "active_checks_enabled",
                       "check_options",
                       "check_type",
                       "current_attempt",               // 5
                       "current_notification_number",
                       "event_handler_enabled",
                       "execution_time",
                       "flap_detection_enabled",
                       "has_been_checked",              // 10
                       "is_executing",
                       "is_flapping",
                       "last_check",
                       "last_notification",
                       "last_state_change",             // 15
                       "latency",
                       "long_plugin_output",
                       "next_check",
                       "notifications_enabled",
                       "obsess_over_host",              // 20
                       "percent_state_change",
                       "perf_data",
                       "plugin_output",
                       "process_performance_data",
                       "scheduled_downtime_depth",      // 25
                       "state",
                       "state_type",
                       "modified_attributes",
                       "last_time_down",
                       "last_time_unreachable",         // 30
                       "last_time_up",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);

    /* add filter by last_refresh and is_executing and all our hosts which are marked as currently running */
    filtered_query = malloc(sizeof(char) * 50 * get_host_count());
    len = sprintf(filtered_query, "%s\nFilter: is_executing = 1\nFilter: last_check >= %d\nOr: 2\n", query, (int)last_refresh);

    /* linear search to get all hosts currently running */
    running = 0;
    hst     = host_list;
    while(hst) {
        if(hst->is_executing) {
            len += sprintf(filtered_query+len, "Filter: name = %s\n", hst->name);
            running++;
            if(running > max_number_of_executing_objects)
                break;
        }
        hst = hst->next;
    }
    if(running > 0) {
        len += sprintf(filtered_query+len, "Or: %d\nOr: 2\n", running);
    }
    /* too many running hosts would blow off our filter, so just fetch everything if we hit the limit */
    if(full_refresh_required || running > max_number_of_executing_objects) {
        my_free(filtered_query);
        filtered_query = strdup(query);
    }
    num = livestatus_query(&answer, (char*)input_source, filtered_query, columns, columns_size);
    my_free(filtered_query);

    /* update our hosts */
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            hst = find_host(row->set[0]);
            if(hst == NULL) {
                logit(NSLOG_INFO_MESSAGE, TRUE, "host '%s' not found, something is seriously wrong\n", row->set[0]);
                exit(EXIT_FAILURE);
            }
            hst->accept_passive_checks          = atoi(row->set[1]);
            hst->checks_enabled                 = atoi(row->set[2]);
            hst->check_options                  = atoi(row->set[3]);
            hst->check_type                     = atoi(row->set[4]);
            hst->current_attempt                = atoi(row->set[5]);
            hst->current_notification_number    = atoi(row->set[6]);
            hst->event_handler_enabled          = atoi(row->set[7]);
            hst->execution_time                 = atof(row->set[8]);
            hst->flap_detection_enabled         = atoi(row->set[9]);
            hst->has_been_checked               = atoi(row->set[10]);
            hst->is_executing                   = atoi(row->set[11]);
            hst->is_flapping                    = atoi(row->set[12]);
            hst->last_check                     = atoi(row->set[13]);
            hst->last_notification              = atoi(row->set[14]);
            hst->last_state_change              = atoi(row->set[15]);
            hst->latency                        = atof(row->set[16]);
            my_free(hst->long_plugin_output);
            hst->long_plugin_output             = strdup(row->set[17]);
            hst->next_check                     = atoi(row->set[18]);
            hst->notifications_enabled          = atoi(row->set[19]);
            hst->obsess                         = atoi(row->set[20]);
            hst->percent_state_change           = atoi(row->set[21]);
            my_free(hst->perf_data);
            hst->perf_data                      = strdup(row->set[22]);
            my_free(hst->plugin_output);
            hst->plugin_output                  = strdup(row->set[23]);
            hst->process_performance_data       = atoi(row->set[24]);
            hst->scheduled_downtime_depth       = atoi(row->set[25]);
            hst->current_state                  = atoi(row->set[26]);
            hst->state_type                     = atoi(row->set[27]);
            hst->modified_attributes            = atol(row->set[28]);
            hst->last_time_down                 = atoi(row->set[29]);
            hst->last_time_unreachable          = atoi(row->set[30]);
            hst->last_time_up                   = atoi(row->set[31]);
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    if(num < 0) {
        logit(NSLOG_INFO_MESSAGE, TRUE, "updating hosts status failed\n");
        return(ERROR);
    }
    timing_point("updated %d hosts\n", num);
    return(OK);
}

/* updates service status based on remote sites data */
int update_service_status_data() {
    int num, running, len;
    service *svc = NULL;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query  = "GET services";
    char *filtered_query;
    char *columns[] = {"host_name",                     // 0
                       "description",
                       "accept_passive_checks",
                       "active_checks_enabled",
                       "check_options",
                       "check_type",                    // 5
                       "current_attempt",
                       "current_notification_number",
                       "event_handler_enabled",
                       "execution_time",
                       "flap_detection_enabled",        // 10
                       "has_been_checked",
                       "is_executing",
                       "is_flapping",
                       "last_check",
                       "last_notification",             // 15
                       "last_state_change",
                       "latency",
                       "long_plugin_output",
                       "next_check",
                       "notifications_enabled",         // 20
                       "obsess_over_service",
                       "percent_state_change",
                       "perf_data",
                       "plugin_output",
                       "process_performance_data",      // 25
                       "scheduled_downtime_depth",
                       "state",
                       "state_type",
                       "modified_attributes",
                       "last_time_ok",                  // 30
                       "last_time_warning",
                       "last_time_unknown",
                       "last_time_critical",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);

    /* add filter by last_refresh and is_executing and all our services which are marked as currently running */
    filtered_query = malloc(sizeof(char) * 50 * get_service_count());
    len = sprintf(filtered_query, "%s\nFilter: is_executing = 1\nFilter: last_check >= %d\nOr: 2\n", query, (int)last_refresh);

    /* linear search to get all services currently running */
    running = 0;
    svc     = service_list;
    while(svc) {
        if(svc->is_executing) {
            len += sprintf(filtered_query+len, "Filter: host_name = %s\nFilter: description = %s\nAnd: 2\n", svc->host_name, svc->description);
            running++;
            if(running > max_number_of_executing_objects)
                break;
        }
        svc = svc->next;
    }
    if(running > 0) {
        len += sprintf(filtered_query+len, "Or: %d\nOr: 2\n", running);
    }
    /* too many running services would blow off our filter, so just fetch everything if we hit the limit */
    if(full_refresh_required || running > max_number_of_executing_objects) {
        my_free(filtered_query);
        filtered_query = strdup(query);
    }
    num = livestatus_query(&answer, (char*)input_source, filtered_query, columns, columns_size);
    my_free(filtered_query);

    if(num > 0) {
        row = answer;
        while(row != NULL && row->set != NULL) {
            svc = find_service(row->set[0], row->set[1]);
            if(svc == NULL) {
                logit(NSLOG_INFO_MESSAGE, TRUE, "service '%s' on hst '%s' not found, something is seriously wrong\n", row->set[1], row->set[0]);
                exit(EXIT_FAILURE);
            }
            svc->accept_passive_checks          = atoi(row->set[2]);
            svc->checks_enabled                 = atoi(row->set[3]);
            svc->check_options                  = atoi(row->set[4]);
            svc->check_type                     = atoi(row->set[5]);
            svc->current_attempt                = atoi(row->set[6]);
            svc->current_notification_number    = atoi(row->set[7]);
            svc->event_handler_enabled          = atoi(row->set[8]);
            svc->execution_time                 = atof(row->set[9]);
            svc->flap_detection_enabled         = atoi(row->set[10]);
            svc->has_been_checked               = atoi(row->set[11]);
            svc->is_executing                   = atoi(row->set[12]);
            svc->is_flapping                    = atoi(row->set[13]);
            svc->last_check                     = atoi(row->set[14]);
            svc->last_notification              = atoi(row->set[15]);
            svc->last_state_change              = atoi(row->set[16]);
            svc->latency                        = atof(row->set[17]);
            my_free(svc->long_plugin_output);
            svc->long_plugin_output             = strdup(row->set[18]);
            svc->next_check                     = atoi(row->set[19]);
            svc->notifications_enabled          = atoi(row->set[20]);
            svc->obsess                         = atoi(row->set[21]);
            svc->percent_state_change           = atoi(row->set[22]);
            my_free(svc->perf_data);
            svc->perf_data                      = strdup(row->set[23]);
            my_free(svc->plugin_output);
            svc->plugin_output                  = strdup(row->set[24]);
            svc->process_performance_data       = atoi(row->set[25]);
            svc->scheduled_downtime_depth       = atoi(row->set[26]);
            svc->current_state                  = atoi(row->set[27]);
            svc->state_type                     = atoi(row->set[28]);
            svc->modified_attributes            = atol(row->set[29]);
            svc->last_time_ok                   = atoi(row->set[30]);
            svc->last_time_warning              = atoi(row->set[31]);
            svc->last_time_unknown              = atoi(row->set[32]);
            svc->last_time_critical             = atoi(row->set[33]);
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    if(num < 0) {
        logit(NSLOG_INFO_MESSAGE, TRUE, "updating service status failed\n");
        return(ERROR);
    }
    timing_point("updated %d services\n", num);
    return(OK);
}

/* updates data from external commands, this helps reducing delay for external commands */
int update_external_commands() {
    glob_t globbuf;
    unsigned int i;
    if(glob(cmds_pattern, 0, NULL, &globbuf) == 0) {
        for(i=0; i<globbuf.gl_pathc;i++) {
            printf("found file: %s\n", globbuf.gl_pathv[i]);
            process_external_commands_from_file(globbuf.gl_pathv[i], TRUE);
            timing_point("processed external commands from %s\n", globbuf.gl_pathv[i]);
        }
        globfree(&globbuf);
        full_refresh_required = TRUE;
    }
    return(OK);
}

/* updates downtimes based on remote sites data */
int update_downtime_data() {
    int num, result;
    unsigned long current_id;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    host *hst = NULL;
    service *svc = NULL;
    char *query  = "GET downtimes";
    char *filtered_query;
    char *columns[] = {"id",                            // 0
                       "host_name",
                       "service_description",
                       "author",
                       "comment",
                       "entry_time",                    // 5
                       "start_time",
                       "end_time",
                       "triggered_by",
                       "type",
                       "duration",                      // 10
                       "fixed",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);

    /* add filter by highest downtime id, we only need new ones */
    filtered_query = malloc(sizeof(char) * 50 * get_service_count());
    sprintf(filtered_query, "%s\nFilter: id > %lu\n", query, highest_downtime_id);

    num = livestatus_query(&answer, (char*)input_source, filtered_query, columns, columns_size);
    my_free(filtered_query);

    if(num > 0) {
        row = answer;
        while(row != NULL && row->set != NULL) {
            current_id = atol(row->set[0]);
            /* host downtimes */
            if(!strcmp(row->set[2], "")) {
                result = add_host_downtime(row->set[1],             // host_name
                                           atoi(row->set[5]),       // entry_time
                                           row->set[3],             // author
                                           row->set[4],             // comment_data
                                           atoi(row->set[6]),       // start_time
                                           0,                       // flex_downtime_start,
                                           atoi(row->set[7]),       // end_time
                                           atoi(row->set[11]),      // fixed
                                           atol(row->set[8]),       // triggered_by
                                           atol(row->set[10]),      // duration
                                           atol(row->set[0]),       // id
                                           FALSE,                   // is_in_effect,
                                           FALSE                    // start_notification_sent
                                        );
                hst    = find_host(row->set[1]);
                hst->scheduled_downtime_depth++;
                if(result != OK) {
                    logit(NSLOG_INFO_MESSAGE, TRUE, "adding host downtime failed (id: %s, host: '%s'), something is seriously wrong\n", row->set[0], row->set[1]);
                    exit(EXIT_FAILURE);
                }
            } else {
                result = add_service_downtime(
                                           row->set[1],             // host_name
                                           row->set[2],             // svc_description
                                           atoi(row->set[5]),       // entry_time
                                           row->set[3],             // author
                                           row->set[4],             // comment_data
                                           atoi(row->set[6]),       // start_time
                                           0,                       // flex_downtime_start,
                                           atoi(row->set[7]),       // end_time
                                           atoi(row->set[11]),      // fixed
                                           atol(row->set[8]),       // triggered_by
                                           atol(row->set[10]),      // duration
                                           atol(row->set[0]),       // id
                                           FALSE,                   // is_in_effect,
                                           FALSE                    // start_notification_sent
                                        );
                svc    = find_service(row->set[1], row->set[2]);
                svc->scheduled_downtime_depth++;
                if(result != OK) {
                    logit(NSLOG_INFO_MESSAGE, TRUE, "adding service downtime failed (id: %s, host: '%s', service: '%s'), something is seriously wrong\n", row->set[0], row->set[1], row->set[2]);
                    exit(EXIT_FAILURE);
                }
            }
            if(current_id > highest_downtime_id)
                highest_downtime_id = current_id;
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    if(num < 0) {
        logit(NSLOG_INFO_MESSAGE, TRUE, "updating downtimes failed\n");
        return(ERROR);
    }
    timing_point("added %d new downtimes\n", num);
    return(OK);
}

/* remove old downtimes */
int remove_old_downtimes() {
    int num, result, removed, found;
    unsigned long current_id;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    scheduled_downtime *temp_downtime, *curr_downtime;
    host *hst = NULL;
    service *svc = NULL;
    char *query  = "GET downtimes";
    char *columns[] = {"id"};
    int columns_size = 1;

    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);

    removed = 0;
    if(num >= 0) {
        // iterate all our downtimes
        temp_downtime = scheduled_downtime_list;
        while(temp_downtime != NULL) {
            found = 0;
            row   = answer;
            // find it in our result
            while(row != NULL && row->set != NULL) {
                current_id = atol(row->set[0]);
                if(temp_downtime->downtime_id == current_id) {
                    found = 1;
                    break;
                }
                row = row->next;
            }
            curr_downtime = temp_downtime;
            temp_downtime = temp_downtime->next;
            if(found == 0) {
                // nothing found, remove
                current_id         = curr_downtime->downtime_id;
                if(curr_downtime->service_description == NULL) {
                    hst    = find_host(curr_downtime->host_name);
                    hst->scheduled_downtime_depth--;
                    result = unschedule_downtime(HOST_DOWNTIME, current_id);
                } else {
                    svc    = find_service(curr_downtime->host_name, curr_downtime->service_description);
                    svc->scheduled_downtime_depth--;
                    result = unschedule_downtime(SERVICE_DOWNTIME, current_id);
                }
                if(result != OK) {
                    logit(NSLOG_INFO_MESSAGE, TRUE, "removing downtime failed (id: %lu), something is seriously wrong\n", current_id);
                    exit(EXIT_FAILURE);
                } else {
                    removed++;
                }
            } else {
            }
        }
    }
    free_livestatus_result(answer, columns_size);
    if(num < 0) {
        logit(NSLOG_INFO_MESSAGE, TRUE, "removing old downtimes failed\n");
        return(ERROR);
    }
    timing_point("removed %d old downtimes\n", removed);
    return(OK);
}

/* updates comments based on remote sites data */
int update_comment_data() {
    int num, result;
    unsigned long current_id;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query  = "GET comments";
    char *filtered_query;
    char *columns[] = {"id",                            // 0
                       "host_name",
                       "service_description",
                       "author",
                       "comment",
                       "entry_time",                    // 5
                       "entry_type",
                       "expire_time",
                       "expires",
                       "persistent",
                       "source",                        // 10
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);

    /* add filter by highest comment id, we only need new ones */
    filtered_query = malloc(sizeof(char) * 50 * get_service_count());
    sprintf(filtered_query, "%s\nFilter: id > %lu\n", query, highest_comment_id);

    num = livestatus_query(&answer, (char*)input_source, filtered_query, columns, columns_size);
    my_free(filtered_query);

    if(num > 0) {
        row = answer;
        while(row != NULL) {
            current_id = atol(row->set[0]);
            /* host comments */
            if(!strcmp(row->set[2], "")) {
                result = add_host_comment(atoi(row->set[6]),    // entry_type
                                          row->set[1],          // host_name
                                          atoi(row->set[5]),    // entry_time
                                          row->set[3],          // author
                                          row->set[4],          // comment_data
                                          current_id,           // comment_id
                                          atoi(row->set[9]),    // persistent
                                          atoi(row->set[8]),    // expires
                                          atoi(row->set[7]),    // expire_time
                                          atoi(row->set[10])    // source
                                        );
                if(result != OK) {
                    logit(NSLOG_INFO_MESSAGE, TRUE, "adding host comment failed (id: %s, host: '%s'), something is seriously wrong\n", row->set[0], row->set[1]);
                    exit(EXIT_FAILURE);
                }
            } else {
                result = add_service_comment(
                                          atoi(row->set[6]),    // entry_type
                                          row->set[1],          // host_name
                                          row->set[2],          // svc_description
                                          atoi(row->set[5]),    // entry_time
                                          row->set[3],          // author
                                          row->set[4],          // comment_data
                                          current_id,           // comment_id
                                          atoi(row->set[9]),    // persistent
                                          atoi(row->set[8]),    // expires
                                          atoi(row->set[7]),    // expire_time
                                          atoi(row->set[10])    // source
                                        );
                if(result != OK) {
                    logit(NSLOG_INFO_MESSAGE, TRUE, "adding service comment failed (id: %s, host: '%s', service: '%s'), something is seriously wrong\n", row->set[0], row->set[1], row->set[2]);
                    exit(EXIT_FAILURE);
                }
            }
            if(current_id > highest_comment_id)
                highest_comment_id = current_id;
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    if(num < 0) {
        logit(NSLOG_INFO_MESSAGE, TRUE, "updating comments failed\n");
        return(ERROR);
    }
    timing_point("added %d new comments\n", num);
    return(OK);
}

/* remove old comments */
int remove_old_comments() {
    int num, result, removed, found;
    unsigned long current_id;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    comment *temp_comment, *curr_comment;
    char *query  = "GET comments";
    char *columns[] = {"id"};
    int columns_size = 1;

    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);

    removed = 0;
    if(num >= 0) {
        // iterate all our comments
        temp_comment = comment_list;
        while(temp_comment != NULL) {
            found = 0;
            row   = answer;
            // find it in our result
            while(row != NULL && row->set != NULL) {
                current_id = atol(row->set[0]);
                if(temp_comment->comment_id == current_id) {
                    found = 1;
                    break;
                }
                row = row->next;
            }
            curr_comment = temp_comment;
            temp_comment = temp_comment->next;
            if(found == 0) {
                // nothing found, remove
                current_id         = curr_comment->comment_id;
                if(curr_comment->service_description == NULL) {
                    result = delete_host_comment(current_id);
                } else {
                    result = delete_service_comment(current_id);
                }
                if(result != OK) {
                    logit(NSLOG_INFO_MESSAGE, TRUE, "removing comment failed (id: %lu), something is seriously wrong\n", current_id);
                    exit(EXIT_FAILURE);
                } else {
                    removed++;
                }
            }
        }
    }
    free_livestatus_result(answer, columns_size);
    if(num < 0) {
        logit(NSLOG_INFO_MESSAGE, TRUE, "removing old comments failed\n");
        return(ERROR);
    }
    timing_point("removed %d old comments\n", removed);
    return(OK);
}

/* updates everything based on remote sites data */
int update_all_runtime_data() {
    if(update_program_status_data() != OK)
        return(ERROR);

    /* directly return if our program status update results in a required restart */
    if(sigrestart == TRUE)
        return(OK);

    if(update_external_commands() != OK)
        return(ERROR);

    if(update_downtime_data() != OK)
        return(ERROR);

    if(remove_old_downtimes() != OK)
        return(ERROR);

    if(update_comment_data() != OK)
        return(ERROR);

    if(remove_old_comments() != OK)
        return(ERROR);

    if(update_host_status_data() != OK)
        return(ERROR);

    if(update_service_status_data() != OK)
        return(ERROR);

    return(OK);
}

/* main refresh loop */
int run_refresh_loop() {
    int errors = 0;
    int result = OK;
    double duration, sleep_remaining;
    struct timeval refresh_start, refresh_end;

    initialize_core();

    /* fetch runtime data once before starting livestatus */
    if(update_all_runtime_data() != OK) {
        deinitialize_core();
        return(ERROR);
    }

    /* send program data to broker, which also starts livestatus */
    broker_program_state(NEBTYPE_PROCESS_EVENTLOOPSTART, NEBFLAG_NONE, NEBATTR_NONE, NULL);

    /* main action, run broker... */
    daemon_mode = FALSE;
    if(verbose)
        logit(NSLOG_INFO_MESSAGE, TRUE, "%s initialized with pid %d...\n", self_name, getpid());
    logit(NSLOG_PROCESS_INFO, TRUE, "started caching %s to %s\n", input_source, output_socket_path);

    /* sleep normal interval because we just have fetched all data */
    usleep(short_shadow_update_interval);

    while(sigshutdown == FALSE && sigrestart == FALSE) {
        full_refresh_required = FALSE;
        gettimeofday(&refresh_start, NULL);
        if(update_all_runtime_data() != OK) {
            program_start = 0;
            result        = ERROR;
            // give remote site a chance to recover
            if(errors > 100)
                break;
            errors++;
        } else {
            errors       = 0;
            result       = OK;
            last_refresh = refresh_start.tv_sec;
        }
        if(sigrestart == TRUE || sigshutdown == TRUE) {
            if(sigrestart == TRUE)
                write_config_files();
            should_write_config = FALSE;
            close(input_socket);
            input_socket = -1;
            break;
        }

        gettimeofday(&refresh_end, NULL);
        duration = tv_delta_f(&refresh_start, &refresh_end);

        /* decide wheter to use long or short sleep interval, start slow interval after 10min runtime and if there are no connections in 10minutes */
        if(shadow_program_restart < refresh_end.tv_sec - 60 && last_connection < refresh_end.tv_sec - 60) {
            /* no connections in last 10minutes, use slow interval */
            sleep_remaining = long_shadow_update_interval - (duration*1000000);
        } else {
            /* use fast interval otherwise */
            sleep_remaining = short_shadow_update_interval - (duration*1000000);
        }
        if(sleep_remaining > 0)
            usleep(sleep_remaining);
        timing_point("refresh loop waiting...\n");
    }

    my_free(dummy_command);
    dummy_command = NULL;
    deinitialize_core();
    my_free(program_version);

    timing_point("Done cleaning up.\n");

    return(result);
}

/* write commands configuration */
int write_commands_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET commands";
    char *columns[] = {"name",
                       "line",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            if(dummy_command == NULL)
                dummy_command = strdup(row->set[0]);
            fprintf(file,"define command {\n");
            fprintf(file,"    command_name          %s\n",   row->set[0]);
            fprintf(file,"    command_line          %s\n\n", row->set[1]); /* extra new line ensures trailing backslashes don't break anything */
            fprintf(file,"}\n");
            row = row->next;
        }
    } else {
        free_livestatus_result(answer, columns_size);
        return(ERROR);
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote commands\n");
    return(OK);
}

/* write timeperiods configuration */
int write_timeperiods_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET timeperiods";
    char *columns[] = {"name",
                       "alias",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            fprintf(file,"define timeperiod {\n");
            fprintf(file,"    timeperiod_name       %s\n", row->set[0]);
            fprintf(file,"    alias                 %s\n", row->set[1]);
            fprintf(file,"}\n");
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote timeperiods\n");
    return(OK);
}

/* write contactgroups configuration */
int write_contactgroups_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET contactgroups";
    char *columns[] = {"name",
                       "alias",
                       "members",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            fprintf(file,"define contactgroup {\n");
            fprintf(file,"    contactgroup_name     %s\n", row->set[0]);
            fprintf(file,"    alias                 %s\n", row->set[1]);
            write_list_attribute(file, "members", row->set[2]);
            fprintf(file,"}\n");
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote contactgroups\n");
    return(OK);
}

/* write hostgroups configuration */
int write_hostgroups_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET hostgroups";
    char *columns[] = {"name",
                       "alias",
                       "members",
                       "notes",
                       "notes_url",
                       "action_url",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            fprintf(file,"define hostgroup {\n");
            fprintf(file,"    hostgroup_name        %s\n", row->set[0]);
            fprintf(file,"    alias                 %s\n", row->set[1]);
            write_list_attribute(file, "members", row->set[2]);
            if(strcmp(row->set[3], ""))
                fprintf(file,"    notes                 %s\n", row->set[3]);
            if(strcmp(row->set[4], ""))
                fprintf(file,"    notes_url             %s\n", row->set[4]);
            if(strcmp(row->set[5], ""))
                fprintf(file,"    action_url            %s\n", row->set[5]);
            fprintf(file,"}\n");
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote hostgroups\n");
    return(OK);
}

/* write servicegroups configuration */
int write_servicegroups_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET servicegroups";
    char *columns[] = {"name",
                       "alias",
                       "members",
                       "notes",
                       "notes_url",
                       "action_url",
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            fprintf(file,"define servicegroup {\n");
            fprintf(file,"    servicegroup_name     %s\n", row->set[0]);
            fprintf(file,"    alias                 %s\n", row->set[1]);
            write_list_attribute(file, "members", row->set[2]);
            if(strcmp(row->set[3], ""))
                fprintf(file,"    notes                 %s\n", row->set[3]);
            if(strcmp(row->set[4], ""))
                fprintf(file,"    notes_url             %s\n", row->set[4]);
            if(strcmp(row->set[5], ""))
                fprintf(file,"    action_url            %s\n", row->set[5]);
            fprintf(file,"}\n");
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote servicegroups\n");
    return(OK);
}

/* write contacts configuration */
int write_contacts_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET contacts";
    char *columns[] = {"name",
                       "alias",
                       "service_notification_period",
                       "host_notification_period",
                       "email",
                       "host_notifications_enabled",    // 5
                       "service_notifications_enabled",
                       "can_submit_commands",
                       "custom_variable_names",
                       "custom_variable_values"
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            fprintf(file,"define contact {\n");
            fprintf(file,"    contact_name                  %s\n", row->set[0]);
            fprintf(file,"    alias                         %s\n", row->set[1]);
            fprintf(file,"    service_notification_period   %s\n", row->set[2]);
            fprintf(file,"    host_notification_period      %s\n", row->set[3]);
            fprintf(file,"    service_notification_commands %s\n", dummy_command);
            fprintf(file,"    host_notification_commands    %s\n", dummy_command);
            if(strcmp(row->set[4], ""))
                fprintf(file,"    email                         %s\n", row->set[4]);
            fprintf(file,"    host_notifications_enabled    %s\n", row->set[5]);
            fprintf(file,"    service_notifications_enabled %s\n", row->set[6]);
            fprintf(file,"    can_submit_commands           %s\n", row->set[7]);
            write_custom_variables(file, row->set[8], row->set[9]);
            fprintf(file,"}\n");
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote contacts\n");
    return(OK);
}

/* write hosts configuration */
int write_hosts_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET hosts";
    char *columns[] = {"name",                      // 0
                       "alias",
                       "address",
                       "check_period",
                       "check_command",
                       "parents",                   // 5
                       "contacts",
                       "notification_period",
                       "check_interval",
                       "retry_interval",
                       "max_check_attempts",        // 10
                       "low_flap_threshold",
                       "high_flap_threshold",
                       "check_freshness",
                       "notification_interval",
                       "first_notification_delay",  // 15
                       "custom_variable_names",
                       "custom_variable_values"
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            fprintf(file,"define host {\n");
            fprintf(file,"    host_name                  %s\n", row->set[0]);
            fprintf(file,"    alias                      %s\n", row->set[1]);
            fprintf(file,"    address                    %s\n", row->set[2]);
            if(strcmp(row->set[3], ""))
                fprintf(file,"    check_period               %s\n", row->set[3]);
            if(strcmp(row->set[4], ""))
                fprintf(file,"    check_command              %s\n", row->set[4]);
            write_list_attribute(file, "parents", row->set[5]);
            write_list_attribute(file, "contacts", row->set[6]);
            if(strcmp(row->set[7], ""))
                fprintf(file,"    notification_period        %s\n", row->set[7]);
            fprintf(file,"    check_interval             %s\n", row->set[8]);
            fprintf(file,"    retry_interval             %s\n", row->set[9]);
            fprintf(file,"    max_check_attempts         %s\n", row->set[10]);
            fprintf(file,"    low_flap_threshold         %s\n", row->set[11]);
            fprintf(file,"    high_flap_threshold        %s\n", row->set[12]);
            fprintf(file,"    check_freshness            %s\n", row->set[13]);
            fprintf(file,"    notification_interval      %s\n", row->set[14]);
            fprintf(file,"    first_notification_delay   %s\n", row->set[15]);
            write_custom_variables(file, row->set[16], row->set[17]);
            fprintf(file,"}\n");
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote hosts\n");
    return(OK);
}

/* write services configuration */
int write_services_configuration(FILE *file) {
    int num;
    result_list *row = NULL;
    result_list *answer = malloc(sizeof(result_list));
    char *query = "GET services";
    char *columns[] = {"host_name",                 // 0
                       "description",
                       "display_name",
                       "check_period",
                       "check_command",
                       "contacts",                  // 5
                       "notification_period",
                       "check_interval",
                       "retry_interval",
                       "max_check_attempts",
                       "low_flap_threshold",        // 10
                       "high_flap_threshold",
                       "check_freshness",
                       "notification_interval",
                       "first_notification_delay",
                       "custom_variable_names",     // 15
                       "custom_variable_values"
    };
    int columns_size = sizeof(columns)/sizeof(columns[0]);
    num = livestatus_query(&answer, (char*)input_source, query, columns, columns_size);
    if(num > 0) {
        row = answer;
        while(row != NULL) {
            fprintf(file,"define service {\n");
            fprintf(file,"    host_name                  %s\n", row->set[0]);
            fprintf(file,"    service_description        %s\n", row->set[1]);
            fprintf(file,"    display_name               %s\n", row->set[2]);
            fprintf(file,"    check_period               %s\n", row->set[3]);
            fprintf(file,"    check_command              %s\n", row->set[4]);
            write_list_attribute(file, "contacts", row->set[5]);
            fprintf(file,"    notification_period        %s\n", row->set[6]);
            fprintf(file,"    check_interval             %s\n", row->set[7]);
            fprintf(file,"    retry_interval             %s\n", row->set[8]);
            fprintf(file,"    max_check_attempts         %s\n", row->set[9]);
            fprintf(file,"    low_flap_threshold         %s\n", row->set[10]);
            fprintf(file,"    high_flap_threshold        %s\n", row->set[11]);
            fprintf(file,"    check_freshness            %s\n", row->set[12]);
            fprintf(file,"    notification_interval      %s\n", row->set[13]);
            fprintf(file,"    first_notification_delay   %s\n", row->set[14]);
            write_custom_variables(file, row->set[15], row->set[16]);
            fprintf(file,"}\n");
            row = row->next;
        }
    }
    free_livestatus_result(answer, columns_size);
    timing_point("wrote services\n");
    return(OK);
}

/* convert ls result char to list and write that into the given file */
/* just replaces listseperator char with comma */
int write_list_attribute(FILE *file, char* attr, char* rawlist) {
    char *tmpstr;
    int i = -1;
    if(!strcmp(rawlist, ""))
        return(OK);
    tmpstr = strdup(rawlist);
    while(tmpstr[++i] != 0) {
        if(tmpstr[i] == 5 || tmpstr[i] == 6)
            tmpstr[i] = 44;
    }
    fprintf(file,"    %s %s\n", attr, tmpstr);
    my_free(tmpstr);
    return(OK);
}

/* write out custum variables */
int write_custom_variables(FILE *file, char* rawnames, char* rawvalues) {
    char *names, *name, *namesp, *values, *value, *valuesp;
    if(!strcmp(rawnames, ""))
        return(OK);
    names   = strdup(rawnames);
    values  = strdup(rawvalues);
    namesp  = names;
    valuesp = values;
    while((name = strsep(&names, "\x5")) != NULL) {
        value = strsep(&values, "\x5");
        fprintf(file,"    _%s %s\n", name, value);
    }
    my_free(namesp);
    my_free(valuesp);
    return(OK);
}