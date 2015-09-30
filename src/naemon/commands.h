#ifndef _COMMANDS_H
#define _COMMANDS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

NAGIOS_BEGIN_DECL
#include <time.h>

/**************************** COMMAND ERRORS *****************************/
#define CMD_ERROR_OK 0 /* No errors encountered */
#define CMD_ERROR_UNKNOWN_COMMAND 1 /* Unknown/unsupported command */
#define CMD_ERROR_MALFORMED_COMMAND 2 /* Command malformed/missing timestamp? */
#define CMD_ERROR_INTERNAL_ERROR 3 /* Internal error */
#define CMD_ERROR_FAILURE 4 /* Command routine failed */
#define CMD_ERROR_PARSE_MISSING_ARG 5 /*Missing required argument for command*/
#define CMD_ERROR_PARSE_EXCESS_ARG 6 /*Too many arguments for command*/
#define CMD_ERROR_PARSE_TYPE_MISMATCH 7 /*Wrong type for argument, the argument could not be parsed*/
#define CMD_ERROR_UNSUPPORTED_ARG_TYPE 8 /*Unsupported argument type - indicative of implementation bug*/
#define CMD_ERROR_VALIDATION_FAILURE 9 /*Invalid value for argument (validator failed)*/
#define CMD_ERROR_UNSUPPORTED_PARSE_MODE 10 /*Unsupported parse mode*/
#define CMD_ERROR_CUSTOM_COMMAND 11 /*Backwards compat. custom command*/

#define GV(NAME) command_argument_get_value(ext_command, NAME)
#define GV_INT(NAME) (*(int *) GV(NAME))
#define GV_BOOL(NAME) (GV_INT(NAME))
#define GV_ULONG(NAME) (*(unsigned long*)GV(NAME))
#define GV_TIMESTAMP(NAME) (*(time_t *) GV(NAME))
#define GV_TIMEPERIOD(NAME) ((struct timeperiod *) GV(NAME))
#define GV_CONTACT(NAME) ((struct contact *) GV(NAME))
#define GV_CONTACTGROUP(NAME) ((struct contactgroup *) GV(NAME))
#define GV_HOST(NAME) ((host *) GV(NAME))
#define GV_HOSTGROUP(NAME) ((struct hostgroup *) GV(NAME))
#define GV_SERVICE(NAME) ((struct service *) GV(NAME))
#define GV_SERVICEGROUP(NAME) ((struct servicegroup *) GV(NAME))
#define GV_STRING(NAME) ((char *) GV(NAME))
#define GV_DOUBLE(NAME) (*(double *) GV(DOUBLE))

typedef enum {
	UNKNOWN_TYPE,
	CONTACT,
	CONTACTGROUP,
	TIMEPERIOD,
	HOST,
	HOSTGROUP,
	SERVICE,
	SERVICEGROUP,
	STRING,
	BOOL,
	INTEGER,
	ULONG,
	TIMESTAMP,
	DOUBLE
} arg_t;

/**
 * Convert a numeric command error code to a text string. The error message
 * is in English.
 * @param error_code The error code to convert
 */
const char *cmd_error_strerror(int error_code);

/*** PARSE MODES ***/

/**
 * XXX: PARSE MODE PLACEHOLDER
 * TODO: This is not yet implemented, and subject to change!
 * Parse a command of the form [<entry time>] <command name>;<arg1>=<value1>;<arg ...>=<value ...>;<argN>=<valueN>.
 * Note the space between the end of the entry time and the name of the command,
 * it is significant. Optional arguments (i.e arguments with default values) can be omitted
 * by not specifying them.
 * */
#define COMMAND_SYNTAX_KV (1 << 1)

/**
 * Parse a command of the form [<entry time>] <command name>;<arg1>;<arg ...>;<argN>.
 * Note the space between the end of the entry time and the name of the command,
 * it is significant. Optional arguments (i.e arguments with default values) can be omitted
 * by replacing their position in the command string with and empty string (semicolons are still
 * required to denote the absence of a value).
 *
 * Command strings in this form rely on the order of the arguments.
 * */
#define COMMAND_SYNTAX_NOKV (1 << 2)


typedef int (*arg_validator_fn)(void *value);

struct external_command_argument;
struct external_command;
typedef int (*ext_command_handler)(const struct external_command *command, time_t entry_time);

/**
 * Create a command from the given parameters. argspec is a string specifying the argument template
 * of the form "<type>=name;<type>=name2" where type is one of [timeperiod, host, hostgroup, service,
 * servicegroup, str, bool, int, ulong, timestamp, double, contact, contactgroup]. argspecs do not
 * support default values/optional arguments or custom validators, so if those are required you need
 * to use the somewhat more laborious command_argument_add() interface.
 * @param cmd The name of the command. Names must be unique within a register. Custom commands are denoted by a leading _ (underscore)
 * @param handler Callback to be invoked for this command on command_execute_handler()
 * @param description Text describing the purpose and any caveats of this command
 * @param argspec Optional argument template specification. Pass NULL if no arguments are required or you want to add them manually with command_argument_add()
 * @return Pointer to a command. To free the object, use command_destroy()
 */
struct external_command /*@null@*/ * command_create(char *cmd, ext_command_handler handler, char *description, char *argspec);

/**
 * Adds a template for one argument to a command.
 * @param command The command to add an argument to
 * @param name The name of the argument
 * @param type The type of the argument, will be used to determine how to validate the argument if no validator callback is provided
 * @param default_value A default value for this argument if no other value is passed. Must validate. Pass NULL if the argument is required (not optional)
 * @param validator Callback that will be called upon parsing of this argument.
 */
void command_argument_add(struct external_command *command, char *name, arg_t type, void * default_value, arg_validator_fn validator);


/**
 * Adds a command to the command register. Commands in the command register are susceptible to parsing.
 * It is an error to deallocate/destroy a command which has been successfully registered.
 * @param command The command to register
 * @param id A unique ID for this command. Mainly intended for backwards compatibility for core commands. A negative value enables automatic ID allocation.
 * @return On success, the allocated ID for the command is returned. On error, a negative value is returned.
 */
int command_register(struct external_command *command, int id);

/**
 * Does a lookup in the command register for a command name.
 * @param command The name of the command
 * @return If found, the command with the given name is returned. Otherwise, NULL is returned.
 */
struct external_command /*@null@*/ * command_lookup(const char *command);

/**
 * Returns the value of an argument for a command. If the command was retrieved by command_parse(),
 * the parsed, validated argument value is returned. Otherwise, the default argument value is returned.
 * @param command Command
 * @param arg_name Name of the command (as specified in command_argument_add())
 * @return A pointer to the value of the argument. Use the GV* macros for convenient casting of argument values.
 */
void * command_argument_get_value(const struct external_command *command, const char *arg_name);

/**
 * Copies the argument value in src to dst. Space for the destination is allocated by the function.
 * @param dst The destination. To free the destination, use free().
 * @param src The source
 * @return On success, 0 is returned. Otherwise, a negative value is returned.
 */
int command_argument_value_copy(void **dst, const void *src, arg_t type);

/**
 * Unregisters and deallocates the given command. This has the effect of
 * disabling the command for future invocations.
 * @param command The command to unregister
 */
void command_unregister(struct external_command *command);

/**
 * Allocates space for and initializes the command register. The register grows as needed on
 * @param initial_size The initial size (number of commands) of the register
 */
void registered_commands_init(int initial_size);

/**
 * Deinitializes the command register including deregistration and deallocation of all currently registered commands.
 */
void registered_commands_deinit(void);

/**
 * Destroys a command.
 */
void command_destroy(struct external_command * command);

/**
 * Parses a string in accordance with the specified mode. The mode is a bitwise or of modes to attempt -
 * one or more of CMD_SYNTAX_NOKV and CMD_SYNTAX_KV - in that order. When a command is successfully parsed,
 * a handle to the matching registered command is returned.
 * @param cmdstr A command string
 * @param mode Parse modes to attempt
 * @param error Pointer to an integer in which to store error codes on failure. This code can be passed to cmd_error_strerror() for conversion to a human readable message.
 * @return The parsed command, or NULL on failure.
 */
struct external_command /*@null@*/ * command_parse(const char * cmdstr, int mode, int * error);

/**
 * Executes the handler associated with a command.
 * @param command Command
 * @return One of OK or ERROR, signifying the success or failure, respectively, of the command handler
 */
int command_execute_handler(const struct external_command * command);

/**
 * For core use only.
 */
void register_core_commands(void);

/* Various accessors */
time_t command_entry_time(const struct external_command * command);
const char *command_raw_arguments(const struct external_command * command);
int command_id(const struct external_command * command);
const char *command_name(const struct external_command * command);


int open_command_file(void);					/* creates the external command file as a named pipe (FIFO) and opens it for reading */
int close_command_file(void);					/* closes and deletes the external command file (FIFO) */

int process_external_command1(char *);                  /* top-level external command processor */
int process_external_command2(int cmd, time_t entry_time, char *args);  /* DEPRECATED: for backwards NEB compatibility only */
int process_external_commands_from_file(char *, int);   /* process external commands in a file */

int process_passive_service_check(time_t, char *, char *, int, char *);
int process_passive_host_check(time_t, char *, int, char *);

/* Internal Command Implementations */

void disable_service_checks(service *);			/* disables a service check */
void enable_service_checks(service *);			/* enables a service check */
void enable_all_notifications(void);                    /* enables notifications on a program-wide basis */
void disable_all_notifications(void);                   /* disables notifications on a program-wide basis */
void enable_service_notifications(service *);		/* enables service notifications */
void disable_service_notifications(service *);		/* disables service notifications */
void enable_host_notifications(host *);			/* enables host notifications */
void disable_host_notifications(host *);		/* disables host notifications */
void enable_and_propagate_notifications(host *, int, int, int, int);	/* enables notifications for all hosts and services beyond a given host */
void disable_and_propagate_notifications(host *, int, int, int, int);	/* disables notifications for all hosts and services beyond a given host */
void schedule_and_propagate_downtime(host *, time_t, char *, char *, time_t, time_t, int, unsigned long, unsigned long); /* schedules downtime for all hosts beyond a given host */
void acknowledge_host_problem(host *, char *, char *, int, int, int);	/* acknowledges a host problem */
void acknowledge_service_problem(service *, char *, char *, int, int, int);	/* acknowledges a service problem */
void remove_host_acknowledgement(host *);		/* removes a host acknowledgement */
void remove_service_acknowledgement(service *);		/* removes a service acknowledgement */
void start_executing_service_checks(void);		/* starts executing service checks */
void stop_executing_service_checks(void);		/* stops executing service checks */
void start_accepting_passive_service_checks(void);	/* starts accepting passive service check results */
void stop_accepting_passive_service_checks(void);	/* stops accepting passive service check results */
void enable_passive_service_checks(service *);	        /* enables passive service checks for a particular service */
void disable_passive_service_checks(service *);         /* disables passive service checks for a particular service */
void start_using_event_handlers(void);			/* enables event handlers on a program-wide basis */
void stop_using_event_handlers(void);			/* disables event handlers on a program-wide basis */
void enable_service_event_handler(service *);		/* enables the event handler for a particular service */
void disable_service_event_handler(service *);		/* disables the event handler for a particular service */
void enable_host_event_handler(host *);			/* enables the event handler for a particular host */
void disable_host_event_handler(host *);		/* disables the event handler for a particular host */
void enable_host_checks(host *);			/* enables checks of a particular host */
void disable_host_checks(host *);			/* disables checks of a particular host */
void start_obsessing_over_service_checks(void);		/* start obsessing about service check results */
void stop_obsessing_over_service_checks(void);		/* stop obsessing about service check results */
void start_obsessing_over_host_checks(void);		/* start obsessing about host check results */
void stop_obsessing_over_host_checks(void);		/* stop obsessing about host check results */
void enable_service_freshness_checks(void);		/* enable service freshness checks */
void disable_service_freshness_checks(void);		/* disable service freshness checks */
void enable_host_freshness_checks(void);		/* enable host freshness checks */
void disable_host_freshness_checks(void);		/* disable host freshness checks */
void enable_performance_data(void);                     /* enables processing of performance data on a program-wide basis */
void disable_performance_data(void);                    /* disables processing of performance data on a program-wide basis */
void start_executing_host_checks(void);			/* starts executing host checks */
void stop_executing_host_checks(void);			/* stops executing host checks */
void start_accepting_passive_host_checks(void);		/* starts accepting passive host check results */
void stop_accepting_passive_host_checks(void);		/* stops accepting passive host check results */
void enable_passive_host_checks(host *);	        /* enables passive host checks for a particular host */
void disable_passive_host_checks(host *);         	/* disables passive host checks for a particular host */
void start_obsessing_over_service(service *);		/* start obsessing about specific service check results */
void stop_obsessing_over_service(service *);		/* stop obsessing about specific service check results */
void start_obsessing_over_host(host *);			/* start obsessing about specific host check results */
void stop_obsessing_over_host(host *);			/* stop obsessing about specific host check results */
void set_host_notification_number(host *, int);		/* sets current notification number for a specific host */
void set_service_notification_number(service *, int);	/* sets current notification number for a specific service */
void enable_contact_host_notifications(contact *);      /* enables host notifications for a specific contact */
void disable_contact_host_notifications(contact *);     /* disables host notifications for a specific contact */
void enable_contact_service_notifications(contact *);   /* enables service notifications for a specific contact */
void disable_contact_service_notifications(contact *);  /* disables service notifications for a specific contact */

int launch_command_file_worker(void);
int shutdown_command_file_worker(void);
int disconnect_command_file_worker(void);

NAGIOS_END_DECL

#endif
