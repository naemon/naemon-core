#ifndef _COMMANDS_H
#define _COMMANDS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <time.h>
#include <glib.h>
#include "objects_contact.h"
#include "objects_host.h"
#include "objects_service.h"

NAGIOS_BEGIN_DECL

/**************************** COMMAND ERRORS *****************************/
enum NmCommandError {
	CMD_ERROR_OK  = 0, /* No errors encountered */
	CMD_ERROR_UNKNOWN_COMMAND, /* Unknown/unsupported command */
	CMD_ERROR_MALFORMED_COMMAND, /* Command malformed/missing timestamp? */
	CMD_ERROR_INTERNAL_ERROR, /* Internal error */
	CMD_ERROR_FAILURE, /* Command routine failed */
	CMD_ERROR_PARSE_MISSING_ARG, /*Missing required argument for command*/
	CMD_ERROR_PARSE_EXCESS_ARG, /*Too many arguments for command*/
	CMD_ERROR_PARSE_TYPE_MISMATCH, /*Wrong type for argument, the argument could not be parsed*/
	CMD_ERROR_UNSUPPORTED_ARG_TYPE, /*Unsupported argument type - indicative of implementation bug*/
	CMD_ERROR_VALIDATION_FAILURE, /*Invalid value for argument (validator failed)*/
	CMD_ERROR_UNSUPPORTED_PARSE_MODE, /*Unsupported parse mode*/
	CMD_ERROR_CUSTOM_COMMAND, /*Backwards compat. custom command*/
};
#define NM_COMMAND_ERROR nm_command_error_quark ()
GQuark nm_command_error_quark (void);

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
struct external_command /*@null@*/ * command_parse(const char * cmdstr, int mode, GError ** error);

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

int process_external_command(char *cmd, int mode, GError **error); /* processes an external command given mode flags */
int process_external_command1(char *cmd); /* DEPRECATED: top-level external old style command processor */
int process_external_command2(int cmd, time_t entry_time, char *args);  /* DEPRECATED: for backwards NEB compatibility only */
int process_external_commands_from_file(char *, int); /* process external commands in a file */

int process_passive_service_check(time_t, char *, char *, int, char *);
int process_passive_host_check(time_t, char *, int, char *);

int launch_command_file_worker(void);
int shutdown_command_file_worker(void);
int disconnect_command_file_worker(void);
int command_worker_get_pid(void);

NAGIOS_END_DECL

#endif
