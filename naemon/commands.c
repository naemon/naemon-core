#include "config.h"
#include "common.h"
#include "comments.h"
#include "downtime.h"
#include "statusdata.h"
#include "perfdata.h"
#include "sretention.h"
#include "broker.h"
#include "workers.h"
#include "commands.h"
#include "events.h"
#include "utils.h"
#include "checks.h"
#include "flapping.h"
#include "notifications.h"
#include "globals.h"
#include "logging.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <poll.h>


static int command_file_fd;
static FILE *command_file_fp;
static int command_file_created = FALSE;

/* The command file worker process */
static struct {
	/* these must come first for check source detection */
	const char *type;
	const char *source_name;
	int pid;
	int sd;
	iocache *ioc;
} command_worker = { "command file", "command file worker", 0, 0, NULL };


/******************************************************************/
/************* EXTERNAL COMMAND WORKER CONTROLLERS ****************/
/******************************************************************/


/* creates external command file as a named pipe (FIFO) and opens it for reading (non-blocked mode) */
int open_command_file(void)
{
	struct stat st;
	int result = 0;

	/* if we're not checking external commands, don't do anything */
	if (check_external_commands == FALSE)
		return OK;

	/* the command file was already created */
	if (command_file_created == TRUE)
		return OK;

	/* reset umask (group needs write permissions) */
	umask(S_IWOTH);

	/* use existing FIFO if possible */
	if (!(stat(command_file, &st) != -1 && (st.st_mode & S_IFIFO))) {

		/* create the external command file as a named pipe (FIFO) */
		if ((result = mkfifo(command_file, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) != 0) {

			logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Could not create external command file '%s' as named pipe: (%d) -> %s.  If this file already exists and you are sure that another copy of Naemon is not running, you should delete this file.\n", command_file, errno, strerror(errno));
			return ERROR;
		}
	}

	/* open the command file for reading (non-blocked) - O_TRUNC flag cannot be used due to errors on some systems */
	/* NOTE: file must be opened read-write for poll() to work */
	if ((command_file_fd = open(command_file, O_RDWR | O_NONBLOCK)) < 0) {

		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Could not open external command file for reading via open(): (%d) -> %s\n", errno, strerror(errno));

		return ERROR;
	}

	/* set a flag to remember we already created the file */
	command_file_created = TRUE;

	return OK;
}


/* closes the external command file FIFO and deletes it */
int close_command_file(void)
{

	/* if we're not checking external commands, don't do anything */
	if (check_external_commands == FALSE)
		return OK;

	/* the command file wasn't created or was already cleaned up */
	if (command_file_created == FALSE)
		return OK;

	/* reset our flag */
	command_file_created = FALSE;

	/* close the command file */
	fclose(command_file_fp);

	return OK;
}

int disconnect_command_file_worker(void) {
	iobroker_unregister(nagios_iobs, command_worker.sd);
	return 0;
}

/* shutdown command file worker thread */
int shutdown_command_file_worker(void)
{
	if (!command_worker.pid)
		return 0;

	iocache_destroy(command_worker.ioc);
	command_worker.ioc = NULL;
	iobroker_close(nagios_iobs, command_worker.sd);
	command_worker.sd = -1;
	kill(command_worker.pid, SIGKILL);
	command_worker.pid = 0;
	return 0;
}



static int command_input_handler(int sd, int events, void *discard)
{
	int ret, cmd_ret;
	char *buf;
	unsigned long size;

	ret = iocache_read(command_worker.ioc, sd);
	log_debug_info(DEBUGL_COMMANDS, 2, "Read %d bytes from command worker\n", ret);
	if (ret == 0) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Command file worker seems to have died. Respawning\n");
		shutdown_command_file_worker();
		launch_command_file_worker();
		return 0;
	}
	while ((buf = iocache_use_delim(command_worker.ioc, "\n", 1, &size))) {
		if (buf[0] == '[') {
			/* raw external command */
			buf[size] = 0;
			log_debug_info(DEBUGL_COMMANDS, 1, "Read raw external command '%s'\n", buf);
		}
		if ((cmd_ret = process_external_command1(buf)) != CMD_ERROR_OK) {
			logit(NSLOG_EXTERNAL_COMMAND | NSLOG_RUNTIME_WARNING, TRUE, "External command error: %s\n", cmd_error_strerror(cmd_ret));
		}

	}
	return 0;
}


/* main controller of command file helper process */
static int command_file_worker(int sd)
{
	iocache *ioc;

	if (open_command_file() == ERROR)
		return (EXIT_FAILURE);

	ioc = iocache_create(65536);
	if (!ioc)
		exit(EXIT_FAILURE);

	while (1) {
		struct pollfd pfd;
		int pollval, ret;
		char *buf;
		unsigned long size;

		/* if our master has gone away, we need to die */
		if (kill(nagios_pid, 0) < 0 && errno == ESRCH) {
			return EXIT_SUCCESS;
		}

		errno = 0;
		/* wait for data to arrive */
		/* select seems to not work, so we have to use poll instead */
		/* 10-15-08 EG check into implementing William's patch @ http://blog.netways.de/2008/08/15/nagios-unter-mac-os-x-installieren/ */
		/* 10-15-08 EG poll() seems broken on OSX - see Jonathan's patch a few lines down */
		pfd.fd = command_file_fd;
		pfd.events = POLLIN;
		pollval = poll(&pfd, 1, 500);

		/* loop if no data */
		if (pollval == 0)
			continue;

		/* check for errors */
		if (pollval == -1) {
			/* @todo printf("Failed to poll() command file pipe: %m\n"); */
			if (errno == EINTR)
				continue;
			return EXIT_FAILURE;
		}

		errno = 0;
		ret = iocache_read(ioc, command_file_fd);
		if (ret < 1) {
			if (errno == EINTR)
				continue;
			return EXIT_FAILURE;
		}

		size = iocache_available(ioc);
		buf = iocache_use_size(ioc, size);
		ret = write(sd, buf, size);
		/*
		 * @todo Add libio to get io_write_all(), which handles
		 * EINTR and EAGAIN properly instead of just exiting.
		 */
		if (ret < 0 && errno != EINTR)
			return EXIT_FAILURE;
	} /* while(1) */
}


int launch_command_file_worker(void)
{
	int ret, sv[2];
	char *str;

	/*
	 * if we're restarting, we may well already have a command
	 * file worker process running, but disconnected. Reconnect if so.
	 */
	if (command_worker.pid && kill(command_worker.pid, 0) == 0) {
		if (!iobroker_is_registered(nagios_iobs, command_worker.sd)) {
			iobroker_register(nagios_iobs, command_worker.sd, NULL, command_input_handler);
		}
		return 0;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Failed to create socketpair for command file worker: %m\n");
		return ERROR;
	}

	command_worker.pid = fork();
	if (command_worker.pid < 0) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Failed to fork() command file worker: %m\n");
		goto err_close;
	}

	if (command_worker.pid) {
		command_worker.ioc = iocache_create(512 * 1024);
		if (!command_worker.ioc) {
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Failed to create I/O cache for command file worker: %m\n");
			goto err_close;
		}

		command_worker.sd = sv[0];
		ret = iobroker_register(nagios_iobs, command_worker.sd, NULL, command_input_handler);
		if (ret < 0) {
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Failed to register command file worker socket %d with io broker %p: %s; errno=%d: %s\n",
			      command_worker.sd, nagios_iobs, iobroker_strerror(ret), errno, strerror(errno));
			iocache_destroy(command_worker.ioc);
			goto err_close;
		}
		logit(NSLOG_INFO_MESSAGE, TRUE, "Successfully launched command file worker with pid %d\n",
		      command_worker.pid);
		return OK;
	}

	/* child goes here */
	close(sv[0]);

	/* make our own process-group so we can be traced into and stuff */
	setpgid(0, 0);

	str = strdup(command_file);
	free_memory(get_global_macros());
	command_file = str;
	exit(command_file_worker(sv[1]));

	/* error conditions for parent */
err_close:
	close(sv[0]);
	close(sv[1]);
	command_worker.pid = 0;
	command_worker.sd = -1;
	return ERROR;
}
struct arg_val {
	arg_t type;
	void * val;
};

struct external_command_argument {
	char *name;
	struct arg_val *argval;
	arg_validator_fn validator;
};

struct external_command
{
	char *name;
	int id;
	time_t entry_time;
	ext_command_handler handler;
	struct external_command_argument **arguments;
	int argc;
	char *description;
	char *raw_arguments;
};


static int registered_commands_sz;
static struct external_command **registered_commands;
static int num_registered_commands;

/* forward declarations */
static struct arg_val * arg_val_copy(struct arg_val * v);
static struct arg_val * arg_val_create(arg_t type, void * v);
#ifndef __func__
# if __STDC_VERSION__ < 199901L
#  if __GNUC__ >= 2
#   define __func__ __FUNCTION__
#  else
#   define __func__ "<unknown>"
#  endif
# endif
#endif
#define log_mem_error() logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Failed to allocate memory in %s", __func__);

static size_t type_sz(arg_t type) {
	switch (type) {
		case BOOL: return sizeof(int);
		case INTEGER: return sizeof(int);
		case TIMESTAMP: return sizeof(time_t);
		case DOUBLE: return sizeof(double);
		case ULONG: return sizeof(unsigned long);
		default: return -1;
	}
}
struct external_command * command_lookup(const char *ext_command)
{
	int i;
	for ( i = 0; i < registered_commands_sz; i++ ){
		if ((registered_commands[i] != NULL) && (0 == strcmp(ext_command, registered_commands[i]->name)))
			return registered_commands[i];
	}
	return NULL;
}

static struct external_command_argument * command_argument_get(const struct external_command * ext_command, const char *argname)
{
	int i;

	if (!ext_command || !argname)
		return NULL;

	for ( i = 0; i < ext_command->argc; i++) {
		if ( strcmp(argname, ext_command->arguments[i]->name) == 0) {
			return ext_command->arguments[i];
		}
	}

	return NULL;
}

static int is_object(arg_t type) {
	switch (type) {
		case CONTACT:
		case CONTACTGROUP:
		case HOST:
		case HOSTGROUP:
		case SERVICE:
		case SERVICEGROUP:
		case TIMEPERIOD:
			return 1;
		default:
			return 0;
	}
}

static int is_stringy(arg_t type) {
	return (type == STRING || is_object(type));
}

static service *resolve_service(char *obj)
{
	char *hostname = NULL, *service_dscr = NULL;
	char *object = NULL;
	service *svc = NULL;
	if ( obj==NULL || (object = strdup (obj)) == NULL) {
		return NULL;
	}
	else if ((hostname = strtok_r(object, ";", &service_dscr)) != NULL) {
		svc = find_service(hostname, service_dscr);
	}
	free(object);
	return svc; /*may be NULL*/
}

static void *resolve_object(arg_t type, void *object)
{
	switch (type) {
		case CONTACT:
			return find_contact((char *) object);
		case CONTACTGROUP:
			return find_contactgroup((char *) object);
		case HOST:
			return find_host((char *) object);
		case HOSTGROUP:
			return find_hostgroup((char *) object);
		case SERVICE:
			return resolve_service((char *) object);
		case SERVICEGROUP:
			return find_servicegroup((char *) object);
		case TIMEPERIOD:
			return find_timeperiod((char *) object);
		default:
			return NULL;
	}
}

time_t command_entry_time(const struct external_command * ext_command)
{
	return ext_command->entry_time;
}

const char *command_name(const struct external_command * ext_command)
{
	return ext_command->name;
}

int command_id(const struct external_command * ext_command)
{
	return ext_command->id;
}

const char *command_raw_arguments(const struct external_command * ext_command)
{
	return ext_command->raw_arguments;
}

void * command_argument_get_value(const struct external_command * ext_command, const char *argname)
{
	struct external_command_argument * arg = NULL;
	void *value = NULL;

	if (!ext_command)
		return NULL;

	if ((arg = command_argument_get(ext_command, argname)) != NULL) {
		value = arg->argval->val;
	}
	else {
		return NULL;
	}

	if (is_object(arg->argval->type)) {
		return resolve_object(arg->argval->type, value);
	}
	else {
		return value;
	}
}

static struct external_command_argument * command_argument_copy(struct external_command_argument *arg) {
	struct external_command_argument * copy;
	copy = (struct external_command_argument *) malloc(sizeof(struct external_command_argument));
	if (!copy) {
		log_mem_error();
		return NULL;
	}
	copy->name = strdup(arg->name);
	copy->validator = arg->validator;
	copy->argval = arg_val_copy(arg->argval);
	return copy;
}

static struct external_command * external_command_copy(struct external_command * ext_command)
{
	int i;
	struct external_command * copy = malloc(sizeof(struct external_command));
	if (!copy) {
		log_mem_error();
		return NULL;
	}
	copy->name = strdup(ext_command->name);
	copy->id = ext_command->id;
	copy->handler = ext_command->handler;
	copy->argc = ext_command->argc;
	copy->arguments = calloc(copy->argc, sizeof(struct external_command_argument *));
	for ( i = 0; i < copy->argc; i++ ) {
		copy->arguments[i] = command_argument_copy(ext_command->arguments[i]);
	}
	copy->description = strdup(ext_command->description);
	copy->raw_arguments = ext_command ->raw_arguments ? strdup(ext_command->raw_arguments) : NULL;
	return copy;

}

static struct external_command * parse_kv_command(const char * cmdstr, int *error)
{
	*error = CMD_ERROR_UNSUPPORTED_PARSE_MODE;
	return NULL;
}

static int validate_contact(void *value)
{
	int ret = 0;
	if (resolve_object(CONTACT, (char *) value) != NULL) {
		ret = 1;
	}
	return ret;
}

static int validate_contactgroup(void *value)
{
	int ret = 0;
	if (resolve_object(CONTACTGROUP, (char *) value) != NULL) {
		ret = 1;
	}
	return ret;
}

static int validate_service(void *value)
{
	int ret = 0;
	if (resolve_object(SERVICE, value) != NULL) {
		ret = 1;
	}
	return ret;
}

static int validate_servicegroup(void *value)
{
	int ret = 0;
	if (resolve_object(SERVICEGROUP, value) != NULL) {
		ret = 1;
	}
	return ret;
}

static int validate_host(void *value)
{
	int ret = 0;
	if(resolve_object(HOST, value) != NULL) {
		ret = 1;
	}
	return ret;
}

static int validate_hostgroup(void *value)
{
	int ret = 0;
	if (resolve_object(HOSTGROUP, value) != NULL) {
		ret = 1;
	}
	return ret;
}

static int validate_timeperiod(void *value)
{
	int ret = 0;
	if (resolve_object(TIMEPERIOD, value) != NULL) {
		ret = 1;
	}
	return ret;
}

static int validate_bool(void *value)
{
	int ret = 1;
	int val = *(int *)value;

	if ( val < 0 || val > 1) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Validation error: BOOL(%d) not in range (0,1).", val);
		ret = 0;
	}
	return ret;
}

static unsigned long parse_ulong(const char *str, int *error)
{
	unsigned long ret = 0;
	char *endptr;
	errno = 0;
	*error = 0;
	ret = strtoul(str, &endptr, 10);
	if(errno != 0) {
		/*error when parsing*/
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Parse error: '%s' while parsing ulong '%s'", strerror(errno), str);
		*error = 1;
	}
	else if (endptr == str) {
		/*no digits at all*/
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Parse error: No digits found in ulong '%s'", str);
		*error = 1;
	}
	else if (*endptr != '\0') {
		/*invalid character*/
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Parse error: Invalid characters (%s) in ulong '%s'", endptr, str);
		*error = 1;
	}
	return ret;
}

static double parse_double(const char *str, int *error) {
	double ret = 0.0;
	char *endptr = NULL;
	errno = 0;
	*error = 0;
	ret = strtod(str, &endptr);
	if (errno != 0) {
		/*don't bother checking whether this was an overflow/underflow, this error should be enough*/
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Parse error: '%s' while parsing double '%s'", strerror(errno), str);
		*error = 1;
	}
	else if(endptr == str) {
		/*no digits*/
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Parse error: No digits found in double '%s'", str);
		*error = 1;
	}
	else if (*endptr != '\0') {
		/*invalid character*/
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Parse error: Invalid characters (%s) in double '%s'", endptr, str);
		*error = 1;
	}
	return ret;
}

static int parse_integer(const char *str, int *error) {
	int ret = 0;
	char *endptr = NULL;
	errno = 0;
	*error = 0;
	ret = strtol(str, &endptr, 10);
	if(errno != 0) {
		/*error when parsing*/
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Parse error: '%s' while parsing integer '%s'", strerror(errno), str);
		*error = 1;
	}
	else if (endptr == str) {
		/*no digits at all*/
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Parse error: No digits found in integer '%s'", str);
		*error = 1;
	}
	else if (*endptr != '\0') {
		/*invalid character*/
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Parse error: Invalid characters (%s) in integer '%s'", endptr, str);
		*error = 1;
	}
	return ret;
}


static const char * arg_t2str(arg_t type)
{
	switch(type) {
		case CONTACT: return "CONTACT";
		case CONTACTGROUP: return "CONTACTGROUP";
		case TIMEPERIOD: return "TIMEPERIOD";
		case HOST: return "HOST";
		case HOSTGROUP: return "HOSTGROUP";
		case SERVICE: return "SERVICE";
		case SERVICEGROUP: return "SERVICEGROUP";
		case STRING: return "STRING";
		case BOOL: return "BOOL";
		case INTEGER: return "INTEGER";
		case ULONG: return "ULONG";
		case TIMESTAMP: return "TIMESTAMP";
		case DOUBLE: return "DOUBLE";
		default: return "Unknwon type";
	}
}
static int parse_arguments(const char *s, struct external_command_argument **args, int argc)
{
	char *scopy, *next, *temp;
	int i = 0, error = 0, ret = CMD_ERROR_OK;

	temp = scopy = strdup(s);
	/* stash ptr start for free()ing, since *s is const and we copy it */
	for (temp = scopy; temp && ret == CMD_ERROR_OK; i++, temp = next ? next + 1 : NULL) {
		next = strchr(temp, ';');
		if (next && i < argc) {
			*next = '\0';
		}

		/*
		 * if the last argument we parse is a string, we allow
		 * semicolons as part of the string
		 */
		if (i == argc - 1 && args[i]->argval->type == STRING && next) {
			*next = ';';
			next = NULL;
		}

		if (i >= argc) {
			/* Too many arguments */
			if (argc)
				ret = CMD_ERROR_PARSE_EXCESS_ARG;
			break;
		}

		/* empty argument, so check for default value */
		if (!*temp) {
			if (NULL == args[i]->argval->val) {
				/*Nope, raise missing arg error*/
				ret = CMD_ERROR_PARSE_MISSING_ARG;
			}
			continue;
		}

		if(!args[i]->argval->val) {
			/* If we don't have a default value for a non-string (strings are strdup'd) type
			 * we need to make room for it here*/
			if (!is_stringy(args[i]->argval->type)) {
				args[i]->argval->val = malloc(type_sz(args[i]->argval->type));
			}
		} else if (is_stringy(args[i]->argval->type)) {
			free(args[i]->argval->val); /*Free before reassignment*/
		}

		log_debug_info(DEBUGL_COMMANDS, 2, "Parsing '%s' as %s\n", temp, arg_t2str(args[i]->argval->type));
		switch (args[i]->argval->type) {
			case CONTACT:
			case CONTACTGROUP:
			case HOST:
			case TIMEPERIOD:
			case STRING:
			case SERVICEGROUP:
			case HOSTGROUP:
				if ((args[i]->argval->val = strdup(temp)) == NULL){
					ret = CMD_ERROR_INTERNAL_ERROR;
				}
				break;
			case SERVICE:
				/* look-ahead for service name*/
				if (!next) {
					/*No service name*/
					ret = CMD_ERROR_PARSE_TYPE_MISMATCH;
					break;
				}
				*next = ';';
				if ((next = strchr(next + 1, ';'))) {
					*next = '\0';
				}
				args[i]->argval->val = strdup(temp);
				if(args[i]->argval->val == NULL) {
					ret = CMD_ERROR_INTERNAL_ERROR;
				}
				break;
			case BOOL:
				*(int *)(args[i]->argval->val) = parse_integer(temp, &error);
				if (error) {
					ret = CMD_ERROR_PARSE_TYPE_MISMATCH;
				}
				break;
			case INTEGER:
				*(int *)(args[i]->argval->val) = parse_integer(temp, &error);
				if (error) {
					ret = CMD_ERROR_PARSE_TYPE_MISMATCH;
				}
				break;
			case ULONG:
				*(unsigned long *)(args[i]->argval->val) = parse_ulong(temp, &error);
				if (error) {
					ret = CMD_ERROR_PARSE_TYPE_MISMATCH;
				}
			case TIMESTAMP:
				*(time_t *)(args[i]->argval->val) = (time_t)parse_ulong(temp, &error);
				if (error) {
					ret = CMD_ERROR_PARSE_TYPE_MISMATCH;
				}
				break;
			case DOUBLE:
				*(double *)(args[i]->argval->val) = parse_double(temp, &error);
				if (error) {
					ret = CMD_ERROR_PARSE_TYPE_MISMATCH;
				}
			default:
				ret = CMD_ERROR_UNSUPPORTED_ARG_TYPE;
				break;
		}
		if (!(args[i]->validator(args[i]->argval->val)))
		{
			ret = CMD_ERROR_VALIDATION_FAILURE;
		}
	}

	free(scopy);

	if (ret != CMD_ERROR_OK)
		return ret;

	/* discount trailing default values */
	while (argc > i && args[i]->argval->val) {
		i++;
	}

	if (argc > i) { /* Still expecting arguments?*/
		return CMD_ERROR_PARSE_MISSING_ARG;
	}

	return ret;
}

int command_execute_handler(const struct external_command * ext_command)
{
	if (!ext_command)
		return ERROR;
	return ext_command->handler(ext_command, ext_command->entry_time);
}

static struct external_command * parse_nokv_command(const char * cmdstr, int *error)
{
	char *temp_ptr = NULL, *args = NULL;
	int parse_error;
	struct external_command * ext_command = NULL, *command2 = NULL;
	char *cmd = NULL;
	char *cmd_name = NULL;
	time_t entry_time = 0L;
	*error = CMD_ERROR_OK;
	if (cmdstr == NULL) {
		*error = CMD_ERROR_MALFORMED_COMMAND;
		return ext_command;
	}
	cmd = strdup(cmdstr);
	/* get the command entry time */
	if((temp_ptr = my_strtok(cmd, "[")) == NULL || (temp_ptr = my_strtok(NULL, "]")) == NULL) {
		*error = CMD_ERROR_MALFORMED_COMMAND;
	}
	else {
		entry_time = (time_t)parse_ulong(temp_ptr, &parse_error);
		if (parse_error != 0) {
			*error = CMD_ERROR_MALFORMED_COMMAND;
		}
		/* get the command name */
		else if((temp_ptr = my_strtok(NULL, ";")) == NULL) {
			*error = CMD_ERROR_MALFORMED_COMMAND;
		}
		else if((cmd_name = (char *)strdup(temp_ptr + 1)) == NULL) {
			*error = CMD_ERROR_INTERNAL_ERROR;
		}
		/* get the command arguments */
		else if((temp_ptr = my_strtok(NULL, "")) == NULL) {
			/*No arguments, this is (possibly) OK*/
			if ((args = (char *)strdup("")) == NULL) {
				log_mem_error();
				*error = CMD_ERROR_INTERNAL_ERROR;
			}
		}
		else if((args = (char *)strdup(temp_ptr)) == NULL) {
			log_mem_error();
			*error = CMD_ERROR_INTERNAL_ERROR;
		}
		else if (cmd_name[0] == '_') {
			/*command*/
			*error = CMD_ERROR_CUSTOM_COMMAND;
			command2 = command_create(cmd_name, NULL, "A custom command", NULL);
			command2->entry_time = entry_time;
			command2->raw_arguments = strdup(args);
		}

		if (*error == CMD_ERROR_OK) {
			/* Find the command */
			if ((ext_command = command_lookup(cmd_name)) == NULL) {
				*error = CMD_ERROR_UNKNOWN_COMMAND;
			}
			else {
				/* Parse & verify arguments*/
				command2 = external_command_copy(ext_command);
				command2->entry_time = entry_time;
				command2->raw_arguments = strdup(args);
				*error = parse_arguments(args, command2->arguments, command2->argc);
				if (*error != CMD_ERROR_OK) {
					command_destroy(command2);
					command2 = NULL;
				}
			}
		}
	}

	free(cmd_name);
	free(args);
	free(cmd);
	return command2;
}

struct external_command /*@null@*/ * command_parse(const char * cmdstr, int mode, int *error)
{
	struct external_command * ext_command;
	ext_command = NULL;
	*error = CMD_ERROR_OK;
	while ((!ext_command) && mode > 0) {
		if (COMMAND_SYNTAX_NOKV & mode) {
			ext_command = parse_nokv_command(cmdstr, error);
			mode ^= COMMAND_SYNTAX_NOKV;
		}
		else if (COMMAND_SYNTAX_KV & mode) {
			ext_command = parse_kv_command(cmdstr, error);
			mode ^=COMMAND_SYNTAX_KV;
		}
		else {
			logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Invalid parse mode (%d) supplied to %s", mode, __func__);
			*error = CMD_ERROR_UNSUPPORTED_PARSE_MODE;
			return NULL;
		}
	}
	if (ext_command == NULL && *error == CMD_ERROR_OK) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: No command parsed but no error code set in %s - this is a bug, please report it", __func__);
		*error = CMD_ERROR_INTERNAL_ERROR;
	}

	if (ext_command != NULL && *error != CMD_ERROR_OK && *error != CMD_ERROR_CUSTOM_COMMAND) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Command parsed but error code set in %s - this is a bug, please report it", __func__);
		*error = CMD_ERROR_INTERNAL_ERROR;
	}
	return ext_command;
}

static struct arg_val * arg_val_copy(struct arg_val * v)
{
	return arg_val_create(v->type, v->val);
}

static int noop_validator(void *value) {
	return 1;
}

static arg_validator_fn default_validator(arg_t type)
{
	switch (type)
	{
		case CONTACT:
			return &validate_contact;
		case CONTACTGROUP:
			return &validate_contactgroup;
		case HOST:
			return &validate_host;
		case HOSTGROUP:
			return &validate_hostgroup;
		case SERVICE:
			return &validate_service;
		case SERVICEGROUP:
			return &validate_servicegroup;
		case TIMEPERIOD:
			return &validate_timeperiod;
		case BOOL:
			return &validate_bool;
		default:
			return &noop_validator;
	}
}
static struct external_command_argument /*@null@*/ * command_argument_create(char *name, struct arg_val *v, arg_validator_fn validator)
{
	struct external_command_argument * arg;

	arg = malloc(sizeof(struct external_command_argument));
	if (validator == NULL) {
		arg->validator = default_validator(v->type);
	}
	else {
		arg->validator = validator;
	}

	if (v->val != NULL && !(arg->validator(v->val)) ) {
		/*Default value does not validate*/
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Refusing to create argument %s with invalid default value",
				name);
		return NULL;
	}
	if ( arg )
	{
		if (!(arg->name = strdup(name))) {
			log_mem_error();
			return NULL;
		}
		arg->argval = v;
		return arg;
	}
	return NULL;
}

static void arg_val_destroy(struct arg_val *argval)
{
	if (!argval) return;
	if (argval->val) { /*may be NULL if no default value set*/
		free(argval->val);
	}
	free(argval);
}

int command_argument_value_copy(void **dst, const void *src, arg_t type) {
	if (src != NULL) {
		if (!is_stringy(type)) {
			if ( (*dst = malloc(type_sz(type))) == NULL ) {
				log_mem_error();
				return -1;
			}
			memcpy(*dst, src, type_sz(type));
		}
		else {
			if ((*dst = strdup(src)) == NULL) {
				log_mem_error();
				return -1;
			}
		}
	}
	else {
		*dst = NULL;
	}
	return 0;
}

static struct arg_val * arg_val_create(arg_t type, void *val)
{
	struct arg_val *out = malloc(sizeof(struct arg_val));

	out->type = type;
	if (command_argument_value_copy(&out->val, val, type) == 0) {
		return out;
	}
	else {
		free(out);
		return NULL;
	}
}

void command_argument_add(struct external_command *ext_command, char *name, arg_t type, void *default_value, arg_validator_fn validator)
{
	struct arg_val *argval;

	if ( command_argument_get(ext_command, name) != NULL) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Refusing to add already defined argument %s for command %s",
				name, ext_command->name);
		return;
	}
	if( (argval = arg_val_create(type, default_value)) == NULL) {
		logit(NSLOG_RUNTIME_ERROR, TRUE, "Error: Failed to create arg_val in %s", __func__);
		return;
	}

	ext_command->arguments = realloc(ext_command->arguments, sizeof(struct external_command_argument) * (ext_command->argc + 1));
	if( !ext_command->arguments ) {
		log_mem_error();
		arg_val_destroy(argval);
		return;
	}

	ext_command->arguments[ext_command->argc] = command_argument_create(name, argval, validator);
	if( ext_command->arguments[ext_command->argc] == NULL) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Failed to create argument %s for command %s in %s",
				name, ext_command->name, __func__);
		return;
	}
	++(ext_command->argc);

}

static void command_argument_destroy(struct external_command_argument *argument)
{
	free(argument->name);
	arg_val_destroy(argument->argval);
	free(argument);
}

void command_destroy(struct external_command * ext_command)
{
	int i;

	if (!ext_command)
		return;

	for (i = 0; i < ext_command->argc; i++) {
		command_argument_destroy(ext_command->arguments[i]);
	}
	free(ext_command->arguments);
	free(ext_command->name);
	free(ext_command->description);
	free(ext_command->raw_arguments);
	free(ext_command);
}

static arg_t parse_type(const char *type_str)
{
	if (!strcmp(type_str, "timeperiod")) {
		return TIMEPERIOD;
	}
	else if (!strcmp(type_str, "host")) {
		return HOST;
	}
	else if (!strcmp(type_str, "hostgroup")) {
		return HOSTGROUP;
	}
	else if (!strcmp(type_str, "service")) {
		return SERVICE;
	}
	else if (!strcmp(type_str, "servicegroup")) {
		return SERVICEGROUP;
	}
	else if (!strcmp(type_str, "str")) {
		return STRING;
	}
	else if (!strcmp(type_str, "bool")) {
		return BOOL;
	}
	else if (!strcmp(type_str, "int")) {
		return INTEGER;
	}
	else if (!strcmp(type_str, "ulong")) {
		return ULONG;
	}
	else if (!strcmp(type_str, "timestamp")) {
		return TIMESTAMP;
	}
	else if (!strcmp(type_str, "double")) {
		return DOUBLE;
	}
	else if (!strcmp(type_str, "contact")) {
		return CONTACT;
	}
	else if (!strcmp(type_str, "contactgroup")) {
		return CONTACTGROUP;
	}
	return UNKNOWN_TYPE;
}

static int command_add_argspec(struct external_command *ext_command, const char *argspec)
{
	char *saveptr = NULL, *saveptr2 = NULL, *s2 = NULL, *s1 = strdup(argspec);
	char *token = NULL, *subtoken;
	arg_t type;
	s2 = s1;
	for (; (token = strtok_r(s1, ";", &saveptr)) != NULL; s1 = NULL) {
		/*type*/
		subtoken = strtok_r(token, "=", &saveptr2);
		if ((type = parse_type(subtoken)) == UNKNOWN_TYPE) {
			free(s2);
			return ERROR;
		}
		command_argument_add(ext_command, saveptr2, type, NULL, NULL);
	}
	free(s2);
	return OK;

}
struct external_command /*@null@*/ * command_create(char *cmd, ext_command_handler handler, char *description, char *arg_spec)
{
	struct external_command *ext_command = NULL;
	if ( cmd && description ) {

		ext_command = (struct external_command *)malloc(sizeof(struct external_command));
		if( ext_command )
		{
			ext_command->name = strdup(cmd);
			ext_command->entry_time = -1;
			ext_command->handler = handler;
			ext_command->arguments = NULL;
			ext_command->argc = 0;
			ext_command->description = strdup(description);
			ext_command->raw_arguments = NULL;
		}
		else {
			log_mem_error();
		}
	}
	else {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Null parameter passed to %s for %s", __func__, cmd ? cmd : "unknown command");
	}

	if (arg_spec) {
		if (command_add_argspec(ext_command, arg_spec) != OK) {
			return NULL;
		}
	}
	return ext_command;
}

static void grow_registered_commands(void)
{
	int i;
	int new_size = registered_commands_sz * 2;
	registered_commands = (struct external_command **) realloc(registered_commands, sizeof(struct external_command *)  *  new_size);
	if (!registered_commands) {
		log_mem_error();
		return;
	}
	for (i = registered_commands_sz; i < new_size; i++) {
		registered_commands[i] = NULL;
	}
	registered_commands_sz = new_size;
}

int command_register(struct external_command *ext_command, int id)
{
	int i;

	if (!ext_command) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Null parameter command passed to %s", __func__);
		return -1;
	}

	/*does this command name already exist in the registry?*/
	if (command_lookup(ext_command->name)) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Refusing to re-register command %s", ext_command->name);
		return -1;
	}

	if (id >= 0) {
		/*id already taken?*/
		if (registered_commands[id] != NULL) {
			logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Refusing to re-register command ID %d", id);
			return -2;
		}
	}
	else if (num_registered_commands < registered_commands_sz)
	{
		if (registered_commands[num_registered_commands] == NULL)
		{
			/*empty slot available at end of registry*/
			id = num_registered_commands;
		}
		else {
			/*last slot filled, slot available somewhere else*/
			for ( i = 0; i < registered_commands_sz; i++) {
				if ( registered_commands[i] == NULL ) {
					id = i;
					break;
				}
			}
		}
	}
	else
	{
		/*no space available, get some*/
		grow_registered_commands();
		id = num_registered_commands;
	}
	ext_command->id = id;
	registered_commands[id] = ext_command;
	++num_registered_commands;
	return id;
}

void registered_commands_init(int initial_size)
{
	if ( registered_commands != NULL ) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning: Refusing double initialize of commands register");
		return;
	}
	registered_commands = (struct external_command **)calloc((size_t)initial_size, sizeof(struct external_command *));
	if(!registered_commands) {
		log_mem_error();
		return;
	}
	registered_commands_sz = initial_size;
	num_registered_commands = 0;
}

void registered_commands_deinit(void)
{
	int i;
	for (i = 0; i < registered_commands_sz; i++) {
		command_unregister(registered_commands[i]);
	}
	num_registered_commands = 0;
	registered_commands_sz = 0;
	free(registered_commands);
	registered_commands = NULL;
}

void command_unregister(struct external_command *ext_command)
{
	int id;
	if ( !ext_command )
		return;

	id = ext_command->id;
	command_destroy(ext_command);
	registered_commands[id] = NULL;
	--num_registered_commands;
}

static int shutdown_handler(const struct external_command *ext_command, time_t entry_time)
{

	if (!schedule_new_event(EVENT_PROGRAM_SHUTDOWN, TRUE, GV_TIMESTAMP("shutdown_time"), FALSE, 0, NULL, FALSE, NULL, NULL, 0))
		return ERROR;
	return OK;
}

static int restart_handler(const struct external_command *ext_command, time_t entry_time)
{
	if (!schedule_new_event(EVENT_PROGRAM_RESTART, TRUE, GV_TIMESTAMP("restart_time"), FALSE, 0, NULL, FALSE, NULL, NULL, 0))
		return ERROR;
	return OK;
}

static int global_command_handler(const struct external_command *ext_command, time_t entry_time)
{
	switch(ext_command->id) {
		case CMD_SAVE_STATE_INFORMATION:
			return save_state_information(FALSE);

		case CMD_READ_STATE_INFORMATION:
			return read_initial_state_information();

		case CMD_ENABLE_NOTIFICATIONS:
			enable_all_notifications();
			return OK;

		case CMD_DISABLE_NOTIFICATIONS:
			disable_all_notifications();
			return OK;

		case CMD_START_EXECUTING_SVC_CHECKS:
			start_executing_service_checks();
			return OK;

		case CMD_STOP_EXECUTING_SVC_CHECKS:
			stop_executing_service_checks();
			return OK;

		case CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS:
			start_accepting_passive_service_checks();
			return OK;

		case CMD_STOP_ACCEPTING_PASSIVE_SVC_CHECKS:
			stop_accepting_passive_service_checks();
			return OK;

		case CMD_START_OBSESSING_OVER_SVC_CHECKS:
			start_obsessing_over_service_checks();
			return OK;

		case CMD_STOP_OBSESSING_OVER_SVC_CHECKS:
			stop_obsessing_over_service_checks();
			return OK;

		case CMD_START_EXECUTING_HOST_CHECKS:
			start_executing_host_checks();
			return OK;

		case CMD_STOP_EXECUTING_HOST_CHECKS:
			stop_executing_host_checks();
			return OK;

		case CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS:
			start_accepting_passive_host_checks();
			return OK;

		case CMD_STOP_ACCEPTING_PASSIVE_HOST_CHECKS:
			stop_accepting_passive_host_checks();
			return OK;

		case CMD_START_OBSESSING_OVER_HOST_CHECKS:
			start_obsessing_over_host_checks();
			return OK;

		case CMD_STOP_OBSESSING_OVER_HOST_CHECKS:
			stop_obsessing_over_host_checks();
			return OK;

		case CMD_ENABLE_EVENT_HANDLERS:
			start_using_event_handlers();
			return OK;

		case CMD_DISABLE_EVENT_HANDLERS:
			stop_using_event_handlers();
			return OK;

		case CMD_ENABLE_FLAP_DETECTION:
			enable_flap_detection_routines();
			return OK;

		case CMD_DISABLE_FLAP_DETECTION:
			disable_flap_detection_routines();
			return OK;

		case CMD_ENABLE_SERVICE_FRESHNESS_CHECKS:
			enable_service_freshness_checks();
			return OK;

		case CMD_DISABLE_SERVICE_FRESHNESS_CHECKS:
			disable_service_freshness_checks();
			return OK;

		case CMD_ENABLE_HOST_FRESHNESS_CHECKS:
			enable_host_freshness_checks();
			return OK;

		case CMD_DISABLE_HOST_FRESHNESS_CHECKS:
			disable_host_freshness_checks();
			return OK;

		case CMD_ENABLE_PERFORMANCE_DATA:
			enable_performance_data();
			return OK;

		case CMD_DISABLE_PERFORMANCE_DATA:
			disable_performance_data();
			return OK;

		case CMD_PROCESS_FILE:
			return process_external_commands_from_file(GV_STRING("file_name"), GV_BOOL("delete"));

		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown global command ID %d", ext_command->id);
			return ERROR;
	}
}

static void foreach_service_on_host(host *target_host, void (*service_fn)(service *))
{
	servicesmember *servicesmember_p = NULL;
	for(servicesmember_p = target_host->services; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
		service_fn(servicesmember_p->service_ptr);
	}

}

static void foreach_service_in_servicegroup(servicegroup *target_servicegroup, void (*service_fn)(service *))
{
	servicesmember *servicesmember_p = NULL;
	for ( servicesmember_p = target_servicegroup->members; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
		service_fn(servicesmember_p->service_ptr);
	}
}

static void foreach_host_in_servicegroup(servicegroup *target_servicegroup, void (*host_fn)(host *))
{
	servicesmember *servicesmember_p = NULL;
	host *last_host =  NULL, *cur_host = NULL;
	for ( servicesmember_p = target_servicegroup->members; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
		cur_host = find_host(servicesmember_p->host_name);
		if (cur_host == NULL || cur_host == last_host) continue; /*Only apply once for each host*/
		host_fn(cur_host);
		last_host = cur_host;
	}
}

static void foreach_host_in_hostgroup(hostgroup *target_hostgroup, void (*host_fn)(host *))
{
	hostsmember *hostsmember_p = NULL;
	for (hostsmember_p = target_hostgroup->members; hostsmember_p != NULL; hostsmember_p = hostsmember_p->next) {
		host_fn(hostsmember_p->host_ptr);
	}
}

static void foreach_service_in_hostgroup(hostgroup *target_hostgroup, void (*service_fn)(service *))
{
	hostsmember *hostsmember_p = NULL;
	for (hostsmember_p = target_hostgroup->members; hostsmember_p != NULL; hostsmember_p = hostsmember_p->next) {
		foreach_service_on_host(hostsmember_p->host_ptr, service_fn);
	}
}

static void foreach_contact_in_contactgroup(contactgroup *target_contactgroup, void (*contact_fn)(contact *))
{
	contactsmember *contactsmember_p = NULL;
	for(contactsmember_p = target_contactgroup->members; contactsmember_p != NULL; contactsmember_p = contactsmember_p->next) {
		contact_fn(contactsmember_p->contact_ptr);
	}
}

static int del_downtime_by_filter_handler(const struct external_command *ext_command, time_t entry_time)
{
	hostgroup *hostgroup_p = NULL;
	hostsmember *hostsmember_p = NULL;
	int deleted = 0;
	switch (ext_command->id) {
		case CMD_DEL_DOWNTIME_BY_HOST_NAME:
			if(delete_downtime_by_hostname_service_description_start_time_comment(
						!strcmp(GV_STRING("hostname"), "") ? NULL : GV("hostname"),
						!strcmp(GV_STRING("service_description"), "") ? NULL : GV("service_description"),
						GV_TIMESTAMP("downtime_start_time"),
						!strcmp(GV_STRING("comment"), "") ? NULL : GV("comment")
						) == 0)
				return ERROR;
			return OK;
		case CMD_DEL_DOWNTIME_BY_HOSTGROUP_NAME:
			hostgroup_p = GV("hostgroup_name");

			for(hostsmember_p = hostgroup_p->members; hostsmember_p != NULL; hostsmember_p = hostsmember_p->next) {
				if (strcmp(GV_STRING("hostname"), "") && !strcmp(hostsmember_p->host_ptr->name, GV("hostname")))
					continue;
				deleted += delete_downtime_by_hostname_service_description_start_time_comment(
							!strcmp(GV_STRING("hostname"), "") ? NULL : GV("hostname"),
							!strcmp(GV_STRING("service_description"), "") ? NULL : GV("service_description"),
							GV_TIMESTAMP("downtime_start_time"),
							!strcmp(GV_STRING("comment"), "") ? NULL : GV("comment")
							);
			}
			if (deleted == 0) {
				return ERROR;
			}
			return OK;
		case CMD_DEL_DOWNTIME_BY_START_TIME_COMMENT:
			/* No args should give an error */
			if (GV_TIMESTAMP("downtime_start_time") == 0 && !strcmp(GV_STRING("comment"), ""))
				return ERROR;

			if (delete_downtime_by_hostname_service_description_start_time_comment(
						NULL, NULL,
						GV_TIMESTAMP("downtime_start_time"),
						!strcmp(GV_STRING("comment"), "") ? NULL : GV("comment")) == 0)
				return ERROR;
			return OK;
		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown downtime filter deletion command ID %d", (ext_command->id));
			return ERROR;
	}
}

static int host_command_handler(const struct external_command *ext_command, time_t entry_time)
{
	host *target_host = NULL;
	servicesmember *servicesmember_p = NULL;
	service *service_p = NULL;
	unsigned long downtime_id = 0L;
	unsigned long duration = 0L;
	time_t preferred_time = 0L;
	time_t next_valid_time = 0L;

	if ( ext_command->id != CMD_DEL_HOST_COMMENT)
		target_host = GV("host_name");

	switch (ext_command->id) {
		case CMD_ADD_HOST_COMMENT:
			return add_new_comment(HOST_COMMENT, USER_COMMENT,
					target_host->name, NULL, entry_time, GV("author"),
					GV("comment"), GV_BOOL("persistent"),
					COMMENTSOURCE_EXTERNAL, FALSE, (time_t)0, NULL
					);
		case CMD_DEL_HOST_COMMENT:
			return delete_host_comment(GV_ULONG("comment_id"));
		case CMD_DELAY_HOST_NOTIFICATION:
			target_host->next_notification = GV_TIMESTAMP("notification_time");
			return OK;
		case CMD_ENABLE_HOST_SVC_CHECKS:
			foreach_service_on_host(target_host, enable_service_checks);
			return OK;
		case CMD_DISABLE_HOST_SVC_CHECKS:
			foreach_service_on_host(target_host, disable_service_checks);
			return OK;
		case CMD_SCHEDULE_HOST_SVC_CHECKS:
			for(servicesmember_p = target_host->services; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
				if((service_p = servicesmember_p->service_ptr) == NULL)
					continue;
				schedule_service_check(service_p, GV_TIMESTAMP("check_time"), CHECK_OPTION_NONE);
			}
			return OK;
		case CMD_DEL_ALL_HOST_COMMENTS:
			return delete_all_host_comments(target_host->name);
		case CMD_ENABLE_HOST_NOTIFICATIONS:
			enable_host_notifications(target_host);
			return OK;
		case CMD_DISABLE_HOST_NOTIFICATIONS:
			disable_host_notifications(target_host);
			return OK;
		case CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
			enable_and_propagate_notifications(target_host, 0, FALSE, TRUE, TRUE);
			return OK;
		case CMD_DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST:
			disable_and_propagate_notifications(target_host, 0, FALSE, TRUE, TRUE);
			return OK;
		case CMD_ENABLE_HOST_SVC_NOTIFICATIONS:
			foreach_service_on_host(target_host, enable_service_notifications);
			return OK;
		case CMD_DISABLE_HOST_SVC_NOTIFICATIONS:
			foreach_service_on_host(target_host, disable_service_notifications);
			return OK;
		case CMD_ACKNOWLEDGE_HOST_PROBLEM:
			acknowledge_host_problem(target_host, GV("author"), GV("comment"), GV_INT("sticky"),
					GV_BOOL("notify"), GV_BOOL("persistent"));
			return OK;
		case CMD_ENABLE_HOST_EVENT_HANDLER:
			enable_host_event_handler(target_host);
			return OK;
		case CMD_DISABLE_HOST_EVENT_HANDLER:
			disable_host_event_handler(target_host);
			return OK;
		case CMD_ENABLE_HOST_CHECK:
			enable_host_checks(target_host);
			return OK;
		case CMD_DISABLE_HOST_CHECK:
			disable_host_checks(target_host);
			return OK;
		case CMD_REMOVE_HOST_ACKNOWLEDGEMENT:
			remove_host_acknowledgement(target_host);
			return OK;
		case CMD_SCHEDULE_FORCED_HOST_SVC_CHECKS:
			for (servicesmember_p = target_host->services; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
				if ((service_p = servicesmember_p->service_ptr) == NULL)
					continue;
				schedule_service_check(service_p, GV_TIMESTAMP("check_time"), CHECK_OPTION_FORCE_EXECUTION);
			}
			return OK;
		case CMD_SCHEDULE_HOST_DOWNTIME:
			if (GV_BOOL("fixed") > 0) {
				duration = (GV_TIMESTAMP("end_time")) - (GV_TIMESTAMP("start_time"));
			}
			else {
				duration = GV_ULONG("duration");
			}
			return schedule_downtime(HOST_DOWNTIME, target_host->name, NULL, entry_time, GV("author"), GV("comment"),
					GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"),
					GV_ULONG("trigger_id"), duration,
					&downtime_id);
		case CMD_ENABLE_HOST_FLAP_DETECTION:
			enable_host_flap_detection(target_host);
			return OK;
		case CMD_DISABLE_HOST_FLAP_DETECTION:
			disable_host_flap_detection(target_host);
			return OK;
		case CMD_DEL_HOST_DOWNTIME:
			return unschedule_downtime(HOST_DOWNTIME, GV_TIMESTAMP("downtime_id"));
		case CMD_SCHEDULE_HOST_SVC_DOWNTIME:
			if (GV_BOOL("fixed") > 0) {
				duration = (GV_TIMESTAMP("end_time")) - (GV_TIMESTAMP("start_time"));
			}
			else {
				duration = GV_ULONG("duration");
			}
			for (servicesmember_p = target_host->services; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
				if ((service_p = servicesmember_p->service_ptr) == NULL)
					continue;
				schedule_downtime(SERVICE_DOWNTIME, target_host->name, service_p->description, entry_time, GV("author"),
						GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"),
						GV_ULONG("trigger_id"), duration,
						&downtime_id);
			}
			return OK;
		case CMD_PROCESS_HOST_CHECK_RESULT:
			return process_passive_host_check(entry_time /*entry time as check time*/, target_host->name, GV_INT("status_code"), GV("plugin_output"));
		case CMD_ENABLE_PASSIVE_HOST_CHECKS:
			enable_passive_host_checks(target_host);
			return OK;
		case CMD_DISABLE_PASSIVE_HOST_CHECKS:
			disable_passive_host_checks(target_host);
			return OK;
		case CMD_SCHEDULE_HOST_CHECK:
			schedule_host_check(target_host, GV_TIMESTAMP("check_time"), CHECK_OPTION_NONE);
			return OK;
		case CMD_SCHEDULE_FORCED_HOST_CHECK:
			schedule_host_check(target_host, GV_TIMESTAMP("check_time"), CHECK_OPTION_FORCE_EXECUTION);
			return OK;
		case CMD_START_OBSESSING_OVER_HOST:
			start_obsessing_over_host(target_host);
			return OK;
		case CMD_STOP_OBSESSING_OVER_HOST:
			stop_obsessing_over_host(target_host);
			return OK;
		case CMD_CHANGE_HOST_EVENT_HANDLER:
			/*disabled*/
			return ERROR;
		case CMD_CHANGE_HOST_CHECK_COMMAND:
			/*disabled*/
			return ERROR;
		case CMD_CHANGE_NORMAL_HOST_CHECK_INTERVAL:

			target_host->check_interval = GV_TIMESTAMP("check_interval");
			/* schedule a host check if previous interval was 0 (checks were not regularly scheduled) */
			if ( target_host->check_interval == 0 && target_host->checks_enabled == TRUE) {

				/* set the host check flag */
				target_host->should_be_scheduled = TRUE;

				/* schedule a check for right now (or as soon as possible) */
				time(&preferred_time);
				if(check_time_against_period(preferred_time, target_host->check_period_ptr) == ERROR) {
					get_next_valid_time(preferred_time, &next_valid_time, target_host->check_period_ptr);
					target_host->next_check = next_valid_time;
					}
				else
					target_host->next_check = preferred_time;

				/* schedule a check if we should */
				if(target_host->should_be_scheduled == TRUE) {
					schedule_host_check(target_host, target_host->next_check, CHECK_OPTION_NONE);
				}

			}
			target_host->modified_attributes |= MODATTR_NORMAL_CHECK_INTERVAL;
			return OK;
		case CMD_CHANGE_MAX_HOST_CHECK_ATTEMPTS:
			target_host->max_attempts = GV_INT("check_attempts");
			target_host->modified_attributes |= MODATTR_MAX_CHECK_ATTEMPTS;

			if(target_host->state_type == HARD_STATE && target_host->current_state != HOST_UP && target_host->current_attempt > 1)
				target_host->current_attempt = target_host->max_attempts;
			return OK;

		case CMD_SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME:
			if (GV_BOOL("fixed") > 0) {
				duration = (GV_TIMESTAMP("end_time")) - (GV_TIMESTAMP("start_time"));
			}
			else {
				duration = GV_ULONG("duration");
			}
			/* schedule downtime for "parent" host */
			schedule_downtime(HOST_DOWNTIME, target_host->name, NULL, entry_time, GV("author"), GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"), GV_ULONG("trigger_id"), duration, &downtime_id);

			/* schedule triggered downtime for all child hosts */
			schedule_and_propagate_downtime(target_host, entry_time, GV("author"), GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"), downtime_id, duration);
			return OK;
		case CMD_ENABLE_HOST_AND_CHILD_NOTIFICATIONS:
			enable_and_propagate_notifications(target_host, 0, TRUE, TRUE, FALSE);
			return OK;
		case CMD_DISABLE_HOST_AND_CHILD_NOTIFICATIONS:
			disable_and_propagate_notifications(target_host, 0, TRUE, TRUE, FALSE);
			return OK;
		case CMD_SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME:
			if (GV_BOOL("fixed") > 0) {
				duration = (GV_TIMESTAMP("end_time")) - (GV_TIMESTAMP("start_time"));
			}
			else {
				duration = GV_ULONG("duration");
			}
			/* schedule downtime for "parent" host */
			schedule_downtime(HOST_DOWNTIME, target_host->name, NULL, entry_time, GV("author"), GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"), GV_ULONG("trigger_id"), duration, &downtime_id);

			/* schedule (non-triggered) downtime for all child hosts */
			schedule_and_propagate_downtime(target_host, entry_time, GV("author"), GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"), 0, duration);
			return OK;
		case CMD_SET_HOST_NOTIFICATION_NUMBER:
			set_host_notification_number(target_host, GV_INT("notification_number"));
			return OK;
		case CMD_CHANGE_HOST_CHECK_TIMEPERIOD:
			my_free(target_host->check_period);
			target_host->check_period = strdup((GV_TIMEPERIOD("check_timeperiod"))->name);
			target_host->check_period_ptr = GV_TIMEPERIOD("check_timeperiod");
			target_host->modified_attributes |= MODATTR_CHECK_TIMEPERIOD;
#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_host, ext_command->id, MODATTR_CHECK_TIMEPERIOD, target_host->modified_attributes, NULL);
#endif

			/* update the status log with the host info */
			update_host_status(target_host, FALSE);
			return OK;
		case CMD_CHANGE_RETRY_HOST_CHECK_INTERVAL:
			target_host->retry_interval = GV_TIMESTAMP("check_interval");
			target_host->modified_attributes |= MODATTR_RETRY_CHECK_INTERVAL;
#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_host, ext_command->id, MODATTR_RETRY_CHECK_INTERVAL, target_host->modified_attributes, NULL);
#endif

			/* update the status log with the host info */
			update_host_status(target_host, FALSE);
			return OK;
		case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
			return host_notification(target_host, NOTIFICATION_CUSTOM, GV("author"), GV("comment"), GV_INT("options"));
		case CMD_CHANGE_HOST_NOTIFICATION_TIMEPERIOD:
			my_free(target_host->notification_period);
			target_host->notification_period = strdup((GV_TIMEPERIOD("notification_timeperiod"))->name);
			target_host->notification_period_ptr = GV_TIMEPERIOD("notification_timeperiod");
			target_host->modified_attributes |= MODATTR_NOTIFICATION_TIMEPERIOD;

#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_host, ext_command->id, MODATTR_NOTIFICATION_TIMEPERIOD, target_host->modified_attributes, NULL);
#endif

			/* update the status log with the host info */
			return update_host_status(target_host, FALSE);


		case CMD_CHANGE_HOST_MODATTR:
			target_host->modified_attributes = GV_ULONG("value");
#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_host, ext_command->id, target_host->modified_attributes, target_host->modified_attributes, NULL);
#endif
			/* update the status log with the host info */
			return update_host_status(target_host, FALSE);

		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown host command ID %d", ext_command->id);
			return ERROR;

	}
}

static int hostgroup_command_handler(const struct external_command *ext_command, time_t entry_time)
{
	hostgroup *target_hostgroup = GV_HOSTGROUP("hostgroup_name");
	switch (ext_command->id) {

		case CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS:
			foreach_host_in_hostgroup(target_hostgroup, enable_host_notifications);
			return OK;
		case CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS:
			foreach_host_in_hostgroup(target_hostgroup, disable_host_notifications);
			return OK;
		case CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS:
			foreach_service_in_hostgroup(target_hostgroup, enable_service_notifications);
			return OK;
		case CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS:
			foreach_service_in_hostgroup(target_hostgroup, disable_service_notifications);
			return OK;
		case CMD_ENABLE_HOSTGROUP_HOST_CHECKS:
			foreach_host_in_hostgroup(target_hostgroup, enable_host_checks);
			return OK;
		case CMD_DISABLE_HOSTGROUP_HOST_CHECKS:
			foreach_host_in_hostgroup(target_hostgroup, disable_host_checks);
			return OK;
		case CMD_ENABLE_HOSTGROUP_PASSIVE_HOST_CHECKS:
			foreach_host_in_hostgroup(target_hostgroup, enable_passive_host_checks);
			return OK;
		case CMD_DISABLE_HOSTGROUP_PASSIVE_HOST_CHECKS:
			foreach_host_in_hostgroup(target_hostgroup, disable_passive_host_checks);
			return OK;
		case CMD_ENABLE_HOSTGROUP_SVC_CHECKS:
			foreach_service_in_hostgroup(target_hostgroup, enable_service_checks);
			return OK;
		case CMD_DISABLE_HOSTGROUP_SVC_CHECKS:
			foreach_service_in_hostgroup(target_hostgroup, disable_service_checks);
			return OK;
		case CMD_ENABLE_HOSTGROUP_PASSIVE_SVC_CHECKS:
			foreach_service_in_hostgroup(target_hostgroup, enable_passive_service_checks);
			return OK;
		case CMD_DISABLE_HOSTGROUP_PASSIVE_SVC_CHECKS:
			foreach_service_in_hostgroup(target_hostgroup, disable_passive_service_checks);
			return OK;
		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown hostgroup comamnd ID %d", ext_command->id);
			return ERROR;
	}
	return ERROR;
}

static int service_command_handler(const struct external_command *ext_command, time_t entry_time)
{
	struct service *target_service = NULL;
	unsigned long downtime_id = 0L;
	time_t next_valid_time = 0L;
	time_t preferred_time = 0L;
	time_t old_interval = 0L;

	if (ext_command->id != CMD_DEL_SVC_COMMENT)
		target_service = GV_SERVICE("service");

	switch(ext_command->id) {
		case CMD_ADD_SVC_COMMENT:
			return add_new_comment(SERVICE_COMMENT, USER_COMMENT,
					target_service->host_name, target_service->description, entry_time, GV("author"),
					GV("comment"), GV_BOOL("persistent"),
					COMMENTSOURCE_EXTERNAL, FALSE, (time_t)0, NULL
					);
		case CMD_DEL_SVC_COMMENT:
			return delete_service_comment(GV_ULONG("comment_id"));

		case CMD_ENABLE_SVC_CHECK:
			enable_service_checks(target_service);
			return OK;
		case CMD_DISABLE_SVC_CHECK:
			disable_service_checks(target_service);
			return OK;
		case CMD_SCHEDULE_SVC_CHECK:
			schedule_service_check(target_service, GV_TIMESTAMP("check_time"), CHECK_OPTION_NONE);
			return OK;
		case CMD_DELAY_SVC_NOTIFICATION:
			target_service->next_notification = GV_TIMESTAMP("notification_time");
			return OK;
		case CMD_DEL_ALL_SVC_COMMENTS:
			return delete_all_comments(SERVICE_COMMENT, target_service->host_name, target_service->description);
		case CMD_ENABLE_SVC_NOTIFICATIONS:
			enable_service_notifications(target_service);
			return OK;
		case CMD_DISABLE_SVC_NOTIFICATIONS:
			disable_service_notifications(target_service);
			return OK;
		case CMD_PROCESS_SERVICE_CHECK_RESULT:
			return process_passive_service_check(entry_time /*entry time as check time*/, target_service->host_name, target_service->description, GV_INT("status_code"), GV("plugin_output"));
		case CMD_ACKNOWLEDGE_SVC_PROBLEM:
			acknowledge_service_problem(target_service, GV("author"), GV("comment"), GV_INT("sticky"), GV_BOOL("notify"), GV_BOOL("persistent"));
			return OK;
		case CMD_ENABLE_PASSIVE_SVC_CHECKS:
			enable_passive_service_checks(target_service);
			return OK;
		case CMD_DISABLE_PASSIVE_SVC_CHECKS:
			disable_passive_service_checks(target_service);
			return OK;
		case CMD_ENABLE_SVC_EVENT_HANDLER:
			enable_service_event_handler(target_service);
			return OK;
		case CMD_DISABLE_SVC_EVENT_HANDLER:
			disable_service_event_handler(target_service);
			return OK;
		case CMD_REMOVE_SVC_ACKNOWLEDGEMENT:
			remove_service_acknowledgement(target_service);
			return OK;
		case CMD_SCHEDULE_FORCED_SVC_CHECK:
			schedule_service_check(target_service, GV_TIMESTAMP("check_time"), CHECK_OPTION_FORCE_EXECUTION);
			return OK;
		case CMD_SCHEDULE_SVC_DOWNTIME:
			return schedule_downtime(SERVICE_DOWNTIME, target_service->host_name, target_service->description, entry_time, GV("author"), GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"), GV_ULONG("trigger_id"), GV_TIMESTAMP("end_time") - GV_TIMESTAMP("start_time"), &downtime_id);
		case CMD_ENABLE_SVC_FLAP_DETECTION:
			enable_service_flap_detection(target_service);
			return OK;
		case CMD_DISABLE_SVC_FLAP_DETECTION:
			disable_service_flap_detection(target_service);
			return OK;
		case CMD_DEL_SVC_DOWNTIME:
			return unschedule_downtime(SERVICE_DOWNTIME, GV_ULONG("downtime_id"));
		case CMD_START_OBSESSING_OVER_SVC:
			start_obsessing_over_service(target_service);
			return OK;
		case CMD_STOP_OBSESSING_OVER_SVC:
			stop_obsessing_over_service(target_service);
			return OK;
		case CMD_CHANGE_SVC_EVENT_HANDLER:
			/*disabled*/
			return ERROR;
		case CMD_CHANGE_SVC_CHECK_COMMAND:
			/*disabled*/
			return ERROR;
		case CMD_CHANGE_NORMAL_SVC_CHECK_INTERVAL:
			/* schedule a service check if previous interval was 0 (checks were not regularly scheduled) */
			old_interval = target_service->check_interval;
			target_service->check_interval = GV_TIMESTAMP("check_interval");
			if(old_interval == 0 && target_service->checks_enabled == TRUE && target_service->check_interval != 0) {
				target_service->should_be_scheduled = TRUE;
				time(&preferred_time);
				if(check_time_against_period(preferred_time, target_service->check_period_ptr) == ERROR) {
					get_next_valid_time(preferred_time, &next_valid_time, target_service->check_period_ptr);
					target_service->next_check = next_valid_time;
				}
				else
					target_service->next_check = preferred_time;

				/* schedule a check if we should */
				if(target_service->should_be_scheduled == TRUE)
					schedule_service_check(target_service, target_service->next_check, CHECK_OPTION_NONE);
			}
			target_service->modified_attributes |= MODATTR_NORMAL_CHECK_INTERVAL;

#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_service, ext_command->id, MODATTR_NORMAL_CHECK_INTERVAL, target_service->modified_attributes, NULL);
#endif

			/* update the status log with the service info */
			return update_service_status(target_service, FALSE);
		case CMD_CHANGE_RETRY_SVC_CHECK_INTERVAL:

			target_service->retry_interval = GV_TIMESTAMP("check_interval");
			/* set the modified service attribute */
			target_service->modified_attributes |= MODATTR_RETRY_CHECK_INTERVAL;

#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_service, ext_command->id, MODATTR_RETRY_CHECK_INTERVAL, target_service->modified_attributes, NULL);
#endif

			/* update the status log with the service info */
			return update_service_status(target_service, FALSE);

		case CMD_CHANGE_MAX_SVC_CHECK_ATTEMPTS:
			target_service->max_attempts = GV_INT("check_attempts");
			/* adjust current attempt number if in a hard state */
			if (target_service->state_type == HARD_STATE && target_service->current_state != STATE_OK && target_service->current_attempt > 1)
				target_service->current_attempt = target_service->max_attempts;

			target_service->modified_attributes |= MODATTR_MAX_CHECK_ATTEMPTS;
#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_service, ext_command->id, MODATTR_MAX_CHECK_ATTEMPTS, target_service->modified_attributes, NULL);
#endif
			/* update the status log with the service info */
			return update_service_status(target_service, FALSE);

		case CMD_SET_SVC_NOTIFICATION_NUMBER:
			set_service_notification_number(target_service, GV_INT("notification_number"));
			return OK;
		case CMD_CHANGE_SVC_CHECK_TIMEPERIOD:
			my_free(target_service->check_period);
			target_service->check_period = strdup((GV_TIMEPERIOD("check_timeperiod"))->name);
			target_service->check_period_ptr = GV("check_timeperiod");
			target_service->modified_attributes |= MODATTR_CHECK_TIMEPERIOD;

#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_service, ext_command->id, MODATTR_CHECK_TIMEPERIOD, target_service->modified_attributes, NULL);
#endif
			/* update the status log with the service info */
			return update_service_status(target_service, FALSE);

		case CMD_CHANGE_SVC_NOTIFICATION_TIMEPERIOD:
			my_free(target_service->notification_period);
			target_service->notification_period = strdup(GV_TIMEPERIOD("notification_timeperiod")->name);
			target_service->notification_period_ptr = GV("notification_timeperiod");
			target_service->modified_attributes |= MODATTR_NOTIFICATION_TIMEPERIOD;

#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_service, ext_command->id, MODATTR_NOTIFICATION_TIMEPERIOD, target_service->modified_attributes, NULL);
#endif

			/* update the status log with the service info */
			return update_service_status(target_service, FALSE);

		case CMD_CHANGE_SVC_MODATTR:
			target_service->modified_attributes = GV_ULONG("value");
#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_service_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_service, ext_command->id, target_service->modified_attributes, target_service->modified_attributes, NULL);
#endif
			/* update the status log with the host info */
			return update_service_status(target_service, FALSE);
		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown host command ID %d", (ext_command->id));
			return ERROR;
	}
}

static int servicegroup_command_handler(const struct external_command *ext_command, time_t entry_time)
{
	servicegroup *target_servicegroup = GV("servicegroup_name");

	servicesmember *servicesmember_p = NULL;
	unsigned long duration = 0L;
	unsigned long downtime_id = 0L;
	host *last_host =  NULL;
	host *host_p =  NULL;
	switch ( ext_command->id ) {
		case CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
			foreach_service_in_servicegroup(target_servicegroup, enable_service_notifications);
			return OK;
		case CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
			foreach_service_in_servicegroup(target_servicegroup, disable_service_notifications);
			return OK;
		case CMD_ENABLE_SERVICEGROUP_SVC_CHECKS:
			foreach_service_in_servicegroup(target_servicegroup, enable_service_checks);
			return OK;
		case CMD_DISABLE_SERVICEGROUP_SVC_CHECKS:
			foreach_service_in_servicegroup(target_servicegroup, disable_service_checks);
			return OK;
		case CMD_ENABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS:
			foreach_service_in_servicegroup(target_servicegroup, enable_passive_service_checks);
			return OK;
		case CMD_DISABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS:
			foreach_service_in_servicegroup(target_servicegroup, disable_passive_service_checks);
			return OK;

		case CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
			foreach_host_in_servicegroup(target_servicegroup, enable_host_notifications);
			return OK;
		case CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
			foreach_host_in_servicegroup(target_servicegroup, disable_host_notifications);
			return OK;
		case CMD_ENABLE_SERVICEGROUP_HOST_CHECKS:
			foreach_host_in_servicegroup(target_servicegroup, enable_host_checks);
			return OK;
		case CMD_DISABLE_SERVICEGROUP_HOST_CHECKS:
			foreach_host_in_servicegroup(target_servicegroup, disable_host_checks);
			return OK;
		case CMD_ENABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS:
			foreach_host_in_servicegroup(target_servicegroup, enable_passive_host_checks);
			return OK;
		case CMD_DISABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS:
			foreach_host_in_servicegroup(target_servicegroup, disable_passive_host_checks);
			return OK;
		case CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
			last_host = NULL;
			if (GV_BOOL("fixed") > 0) {
				duration = (GV_TIMESTAMP("end_time")) - (GV_TIMESTAMP("start_time"));
			}
			else {
				duration = GV_ULONG("duration");
			}
			for(servicesmember_p = target_servicegroup->members; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
				host_p = find_host(servicesmember_p->host_name);
				if(host_p == NULL)
					continue;
				if(last_host == host_p)
					continue;
				schedule_downtime(HOST_DOWNTIME, servicesmember_p->host_name, NULL, entry_time, GV("author"), GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"), GV_ULONG("trigger_id"), duration, &downtime_id);
				last_host = host_p;
			}
			return OK;
		case CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:
			if (GV_BOOL("fixed") > 0) {
				duration = (GV_TIMESTAMP("end_time")) - (GV_TIMESTAMP("start_time"));
			}
			else {
				duration = GV_ULONG("duration");
			}
			for(servicesmember_p = target_servicegroup->members; servicesmember_p != NULL; servicesmember_p = servicesmember_p->next) {
				if ( schedule_downtime(SERVICE_DOWNTIME, servicesmember_p->host_name, servicesmember_p->service_description, entry_time, GV("author"), GV("comment"), GV_TIMESTAMP("start_time"), GV_TIMESTAMP("end_time"), GV_BOOL("fixed"), GV_ULONG("trigger_id"), duration, &downtime_id) != OK )
					return ERROR;
			}
			return OK;
		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown servicegroup command ID %d", (ext_command->id));
			return ERROR;
	}
}
static int contact_command_handler(const struct external_command *ext_command, time_t entry_time)
{
	contact *target_contact = GV("contact_name");
	switch ( ext_command->id ) {
		case CMD_DISABLE_CONTACT_SVC_NOTIFICATIONS:
			disable_contact_service_notifications(target_contact);
			return OK;
		case CMD_ENABLE_CONTACT_SVC_NOTIFICATIONS:
			enable_contact_service_notifications(target_contact);
			return OK;
		case CMD_DISABLE_CONTACT_HOST_NOTIFICATIONS:
			disable_contact_host_notifications(target_contact);
			return OK;
		case CMD_ENABLE_CONTACT_HOST_NOTIFICATIONS:
			enable_contact_host_notifications(target_contact);
			return OK;
		case CMD_CHANGE_CONTACT_HOST_NOTIFICATION_TIMEPERIOD:
			my_free(target_contact->host_notification_period);
			target_contact->host_notification_period = strdup(GV_TIMEPERIOD("notification_timeperiod")->name);
			target_contact->host_notification_period_ptr = GV_TIMEPERIOD("notification_timeperiod");
			target_contact->modified_host_attributes |= MODATTR_NOTIFICATION_TIMEPERIOD;
			target_contact->modified_service_attributes |= MODATTR_NONE;

#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_contact_data(NEBTYPE_ADAPTIVECONTACT_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_contact, ext_command->id, MODATTR_NONE, target_contact->modified_attributes, MODATTR_NOTIFICATION_TIMEPERIOD, target_contact->modified_host_attributes, MODATTR_NONE, target_contact->modified_service_attributes, NULL);
#endif
			/* update the status log with the contact info */
			return update_contact_status(target_contact, FALSE);

		case CMD_CHANGE_CONTACT_SVC_NOTIFICATION_TIMEPERIOD:
			my_free(target_contact->service_notification_period);
			target_contact->service_notification_period = strdup(GV_TIMEPERIOD("notification_timeperiod")->name);
			target_contact->service_notification_period_ptr = GV_TIMEPERIOD("notification_timeperiod");
			target_contact->modified_host_attributes |= MODATTR_NONE;
			target_contact->modified_service_attributes |= MODATTR_NOTIFICATION_TIMEPERIOD;

#ifdef USE_EVENT_BROKER
			/* send data to event broker */
			broker_adaptive_contact_data(NEBTYPE_ADAPTIVECONTACT_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, target_contact, ext_command->id, MODATTR_NONE, target_contact->modified_attributes, MODATTR_NONE, target_contact->modified_host_attributes, MODATTR_NOTIFICATION_TIMEPERIOD, target_contact->modified_service_attributes, NULL);
#endif
			/* update the status log with the contact info */
			return update_contact_status(target_contact, FALSE);

		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown contact command ID %d", (ext_command->id));
			return ERROR;
	}
}

static int contactgroup_command_handler(const struct external_command *ext_command, time_t entry_time)
{
	contactgroup *target_contactgroup = GV("contactgroup_name");
	switch ( ext_command->id ) {
		case CMD_DISABLE_CONTACTGROUP_SVC_NOTIFICATIONS:
			foreach_contact_in_contactgroup(target_contactgroup, disable_contact_service_notifications);
			return OK;
		case CMD_ENABLE_CONTACTGROUP_SVC_NOTIFICATIONS:
			foreach_contact_in_contactgroup(target_contactgroup, enable_contact_service_notifications);
			return OK;
		case CMD_DISABLE_CONTACTGROUP_HOST_NOTIFICATIONS:
			foreach_contact_in_contactgroup(target_contactgroup, disable_contact_host_notifications);
			return OK;
		case CMD_ENABLE_CONTACTGROUP_HOST_NOTIFICATIONS:
			foreach_contact_in_contactgroup(target_contactgroup, enable_contact_host_notifications);
			return OK;
		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown contactgroup command ID %d", (ext_command->id));
			return ERROR;
	}
}

static int change_custom_var_handler(const struct external_command *ext_command, time_t entry_time) {
	customvariablesmember *customvariablesmember_p = NULL;
	char *varname;
	int x = 0;
	switch ( ext_command->id ) {
		case CMD_CHANGE_CUSTOM_SVC_VAR:
			customvariablesmember_p = ((service *)GV("service"))->custom_variables;
			break;

		case CMD_CHANGE_CUSTOM_HOST_VAR:
			customvariablesmember_p = ((host *)GV("host_name"))->custom_variables;
			break;

		case CMD_CHANGE_CUSTOM_CONTACT_VAR:
			customvariablesmember_p = ((contact *)GV("contact_name"))->custom_variables;
			break;
		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown custom variables modification command ID %d", (ext_command->id));
			return ERROR;
	}
	varname = strdup(GV("varname"));
	/* capitalize the custom variable name */
	for(x = 0; varname[x] != '\x0'; x++)
		varname[x] = toupper(varname[x]);

	/* find the proper variable */
	for (; customvariablesmember_p != NULL; customvariablesmember_p = customvariablesmember_p->next) {

		/* we found the variable, so update the value */
		if(!strcmp(varname, customvariablesmember_p->variable_name)) {

			/* update the value */
			if(customvariablesmember_p->variable_value)
				my_free(customvariablesmember_p->variable_value);
			customvariablesmember_p->variable_value = strdup(GV("varvalue"));

			/* mark the variable value as having been changed */
			customvariablesmember_p->has_been_modified = TRUE;


			break;
		}

	}


	my_free(varname);
	switch ( ext_command->id ) {
		case CMD_CHANGE_CUSTOM_SVC_VAR:
			((service *)GV("service"))->modified_attributes |= MODATTR_CUSTOM_VARIABLE;
			return update_service_status(GV("service"), FALSE);
			break;
		case CMD_CHANGE_CUSTOM_HOST_VAR:
			((host *)GV("host_name"))->modified_attributes |= MODATTR_CUSTOM_VARIABLE;
			return update_host_status(GV("host_name"), FALSE);
			break;
		case CMD_CHANGE_CUSTOM_CONTACT_VAR:
			((contact *)GV("contact_name"))->modified_attributes |= MODATTR_CUSTOM_VARIABLE;
			return update_contact_status(GV("contact_name"), FALSE);
			break;
		default:
			logit(NSLOG_RUNTIME_ERROR, TRUE, "Unknown custom variables modification command ID %d", (ext_command->id));
			return ERROR;
	}
}

void register_core_commands(void)
{
	struct external_command * core_command = NULL;
	time_t default_timestamp = 0;
	core_command = command_create("ADD_HOST_COMMENT", host_command_handler,
			"This command is used to add a comment for the specified host.  If you work with other administrators, you may find it useful to share information about a host that is having problems if more than one of you may be working on it.  If you do not check the 'persistent' option, the comment will be automatically be deleted at the the next program restarted.", "host=host_name;bool=persistent;str=author;str=comment");
	command_register(core_command, CMD_ADD_HOST_COMMENT);

	core_command = command_create("DEL_HOST_COMMENT", host_command_handler,
			"This command is used to delete a specific host comment.", "ulong=comment_id");
	command_register(core_command, CMD_DEL_HOST_COMMENT);

	core_command = command_create("ADD_SVC_COMMENT", service_command_handler,
			"This command is used to add a comment for the specified service.  If you work with other administrators, you may find it useful to share information about a host or service that is having problems if more than one of you may be working on it.  If you do not check the 'persistent' option, the comment will automatically be deleted at the next program restart.", "service=service;bool=persistent;str=author;str=comment");
	command_register(core_command, CMD_ADD_SVC_COMMENT);

	core_command = command_create("DEL_SVC_COMMENT", service_command_handler,
			"This command is used to delete a specific service comment.", "ulong=comment_id");
	command_register(core_command, CMD_DEL_SVC_COMMENT);

	core_command = command_create("ENABLE_SVC_CHECK", service_command_handler,
			"This command is used to enable active checks of a service.", "service=service");
	command_register(core_command, CMD_ENABLE_SVC_CHECK);

	core_command = command_create("DISABLE_SVC_CHECK", service_command_handler,
			"This command is used to disable active checks of a service.", "service=service");
	command_register(core_command, CMD_DISABLE_SVC_CHECK);

	core_command = command_create("SCHEDULE_SVC_CHECK", service_command_handler,
			"Schedules the next active check of a specified service at 'check_time'. The 'check_time' argument is specified in time_t format (seconds since the UNIX epoch). Note that the service may not actually be checked at the time you specify. This could occur for a number of reasons: active checks are disabled on a program-wide or service-specific basis, the service is already scheduled to be checked at an earlier time, etc. If you want to force the service check to occur at the time you specify, look at the SCHEDULE_FORCED_SVC_CHECK command.", "service=service;timestamp=check_time");
	command_register(core_command, CMD_SCHEDULE_SVC_CHECK);

	core_command = command_create("DELAY_SVC_NOTIFICATION", service_command_handler,
			"Delays the next notification for a parciular service until 'notification_time'. The 'notification_time' argument is specified in time_t format (seconds since the UNIX epoch). Note that this will only have an affect if the service stays in the same problem state that it is currently in. If the service changes to another state, a new notification may go out before the time you specify in the 'notification_time' argument.", "service=service;timestamp=notification_time");
	command_register(core_command, CMD_DELAY_SVC_NOTIFICATION);

	core_command = command_create("DELAY_HOST_NOTIFICATION", host_command_handler,
			"Delays the next notification for a parciular service until 'notification_time'. The 'notification_time' argument is specified in time_t format (seconds since the UNIX epoch). Note that this will only have an affect if the service stays in the same problem state that it is currently in. If the service changes to another state, a new notification may go out before the time you specify in the 'notification_time' argument.", "host=host_name;timestamp=notification_time");
	command_register(core_command, CMD_DELAY_HOST_NOTIFICATION);

	core_command = command_create("DISABLE_NOTIFICATIONS", global_command_handler,
			"Disables host and service notifications on a program-wide basis.", NULL);
	command_register(core_command, CMD_DISABLE_NOTIFICATIONS);

	core_command = command_create("ENABLE_NOTIFICATIONS", global_command_handler,
			"Enables host and service notifications on a program-wide basis.", NULL);
	command_register(core_command, CMD_ENABLE_NOTIFICATIONS);

	core_command = command_create("RESTART_PROCESS", restart_handler,
			"Restarts the Naemon process.", NULL);
	command_argument_add(core_command, "restart_time", TIMESTAMP, &default_timestamp, NULL);
	command_register(core_command, CMD_RESTART_PROCESS);

	core_command = command_create("RESTART_PROGRAM", restart_handler,
			"Restarts the Naemon process.", NULL);
	command_argument_add(core_command, "restart_time", TIMESTAMP, &default_timestamp, NULL);
	command_register(core_command, -1);

	core_command = command_create("SHUTDOWN_PROCESS", shutdown_handler,
			"Shuts down the Naemon process.", NULL);
	command_argument_add(core_command, "shutdown_time", TIMESTAMP, &default_timestamp, NULL);
	command_register(core_command, CMD_SHUTDOWN_PROCESS);

	core_command = command_create("SHUTDOWN_PROGRAM", shutdown_handler,
			"Shuts down the Naemon process.", NULL);
	command_argument_add(core_command, "shutdown_time", TIMESTAMP, &default_timestamp, NULL);
	command_register(core_command, -1);

	core_command = command_create("ENABLE_HOST_SVC_CHECKS", host_command_handler,
			"Enables active checks of all services on the specified host.", "host=host_name");
	command_register(core_command, CMD_ENABLE_HOST_SVC_CHECKS);

	core_command = command_create("DISABLE_HOST_SVC_CHECKS", host_command_handler,
			"Disables active checks of all services on the specified host", "host=host_name");
	command_register(core_command, CMD_DISABLE_HOST_SVC_CHECKS);

	core_command = command_create("SCHEDULE_HOST_SVC_CHECKS", host_command_handler,
			"Schedules the next active check of all services on a particular host at 'check_time'. The 'check_time' argument is specified in time_t format (seconds since the UNIX epoch). Note that the services may not actually be checked at the time you specify. This could occur for a number of reasons: active checks are disabled on a program-wide or service-specific basis, the services are already scheduled to be checked at an earlier time, etc. If you want to force the service checks to occur at the time you specify, look at the SCHEDULE_FORCED_HOST_SVC_CHECKS command.", "host=host_name;timestamp=check_time");
	command_register(core_command, CMD_SCHEDULE_HOST_SVC_CHECKS);

	/* Not adding unimplemented CMD_DELAY_HOST_SVC_NOTIFICATIONS*/

	core_command = command_create("DEL_ALL_HOST_COMMENTS", host_command_handler,
			"Deletes all comments associated with a particular host.", "host=host_name");
	command_register(core_command, CMD_DEL_ALL_HOST_COMMENTS);

	core_command = command_create("DEL_ALL_SVC_COMMENTS", service_command_handler,
			"Deletes all comments associated with a particular service.", "service=service");
	command_register(core_command, CMD_DEL_ALL_SVC_COMMENTS);

	core_command = command_create("ENABLE_SVC_NOTIFICATIONS", service_command_handler,
			"Enables notifications for a particular service. Notifications will be sent out for the service only if notifications are enabled on a program-wide basis as well.", "service=service");
	command_register(core_command, CMD_ENABLE_SVC_NOTIFICATIONS);

	core_command = command_create("DISABLE_SVC_NOTIFICATIONS", service_command_handler,
			"Disables notifications for a particular service.", "service=service");
	command_register(core_command, CMD_DISABLE_SVC_NOTIFICATIONS);

	core_command = command_create("ENABLE_HOST_NOTIFICATIONS", host_command_handler,
			"Enables notifications for a particular host. Notifications will be sent out for the host only if notifications are enabled on a program-wide basis as well.", "host=host_name");
	command_register(core_command, CMD_ENABLE_HOST_NOTIFICATIONS);

	core_command = command_create("DISABLE_HOST_NOTIFICATIONS", host_command_handler,
			"Disables notifications for a particular host.", "host=host_name");
	command_register(core_command, CMD_DISABLE_HOST_NOTIFICATIONS);

	core_command = command_create("ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST", host_command_handler,
			"Enables notifications for all hosts and services 'beyond' (e.g. on all child hosts of) the specified host. The current notification setting for the specified host is not affected. Notifications will only be sent out for these hosts and services if notifications are also enabled on a program-wide basis.", "host=host_name");
	command_register(core_command, CMD_ENABLE_ALL_NOTIFICATIONS_BEYOND_HOST);

	core_command = command_create("DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST", host_command_handler,
			"Disables notifications for all hosts and services 'beyond' (e.g. on all child hosts of) the specified host. The current notification setting for the specified host is not affected.", "host=host_name");
	command_register(core_command, CMD_DISABLE_ALL_NOTIFICATIONS_BEYOND_HOST);

	core_command = command_create("ENABLE_HOST_SVC_NOTIFICATIONS", host_command_handler,
			"Enables notifications for all services on the specified host. Note that notifications will not be sent out if notifications are disabled on a program-wide basis.", "host=host_name");
	command_register(core_command, CMD_ENABLE_HOST_SVC_NOTIFICATIONS);

	core_command = command_create("DISABLE_HOST_SVC_NOTIFICATIONS", host_command_handler,
			"Disables notifications for all services on the specified host.", "host=host_name");
	command_register(core_command, CMD_DISABLE_HOST_SVC_NOTIFICATIONS);

	core_command = command_create("PROCESS_SERVICE_CHECK_RESULT", service_command_handler,
			"This is used to submit a passive check result for a particular service. The 'status_code' field should be one of the following: 0=OK, 1=WARNING, 2=CRITICAL, 3=UNKNOWN. The 'plugin_output' field contains text output from the service check, along with optional performance data.", "service=service;int=status_code;str=plugin_output");
	command_register(core_command, CMD_PROCESS_SERVICE_CHECK_RESULT);

	core_command = command_create("SAVE_STATE_INFORMATION", global_command_handler,
			"Causes Naemon to save all current monitoring status information to the state retention file. Normally, state retention information is saved before the Naemon process shuts down and (potentially) at regularly scheduled intervals. This command allows you to force Naemon to save this information to the state retention file immediately. This does not affect the current status information in the Naemon process.", NULL);
	command_register(core_command, CMD_SAVE_STATE_INFORMATION);

	core_command = command_create("READ_STATE_INFORMATION", global_command_handler,
			"Causes Naemon to load all current monitoring status information from the state retention file. Normally, state retention information is loaded when the Naemon process starts up and before it starts monitoring. WARNING: This command will cause Naemon to discard all current monitoring status information and use the information stored in state retention file! Use with care.", NULL);
	command_register(core_command, CMD_READ_STATE_INFORMATION);

	core_command = command_create("ACKNOWLEDGE_HOST_PROBLEM", host_command_handler,
			"Allows you to acknowledge the current problem for the specified host. By acknowledging the current problem, future notifications (for the same host state) are disabled. If the 'sticky' option is set to two (2), the acknowledgement will remain until the host returns to an UP state. Otherwise the acknowledgement will automatically be removed when the host changes state. If the 'notify' option is set to one (1), a notification will be sent out to contacts indicating that the current host problem has been acknowledged. If the 'persistent' option is set to one (1), the comment associated with the acknowledgement will survive across restarts of the Naemon process. If not, the comment will be deleted the next time Naemon restarts.", "host=host_name;int=sticky;bool=notify;bool=persistent;str=author;str=comment");
	command_register(core_command, CMD_ACKNOWLEDGE_HOST_PROBLEM);

	core_command = command_create("ACKNOWLEDGE_SVC_PROBLEM", service_command_handler,
			"Allows you to acknowledge the current problem for the specified service. By acknowledging the current problem, future notifications (for the same servicestate) are disabled. If the 'sticky' option is set to two (2), the acknowledgement will remain until the service returns to an OK state. Otherwise the acknowledgement will automatically be removed when the service changes state. If the 'notify' option is set to one (1), a notification will be sent out to contacts indicating that the current service problem has been acknowledged. If the 'persistent' option is set to one (1), the comment associated with the acknowledgement will survive across restarts of the Naemon process. If not, the comment will be deleted the next time Naemon restarts.", "service=service;int=sticky;bool=notify;bool=persistent;str=author;str=comment");
	command_register(core_command, CMD_ACKNOWLEDGE_SVC_PROBLEM);

	core_command = command_create("START_EXECUTING_SVC_CHECKS", global_command_handler,
			"Enables active checks of services on a program-wide basis.", NULL);
	command_register(core_command, CMD_START_EXECUTING_SVC_CHECKS);

	core_command = command_create("STOP_EXECUTING_SVC_CHECKS", global_command_handler,
			"Disables active checks of services on a program-wide basis.", NULL);
	command_register(core_command, CMD_STOP_EXECUTING_SVC_CHECKS);

	core_command = command_create("START_ACCEPTING_PASSIVE_SVC_CHECKS", global_command_handler,
			"Enables passive service checks on a program-wide basis.", NULL);
	command_register(core_command, CMD_START_ACCEPTING_PASSIVE_SVC_CHECKS);

	core_command = command_create("STOP_ACCEPTING_PASSIVE_SVC_CHECKS", global_command_handler,
			"Disables passive service checks on a program-wide basis.", NULL);
	command_register(core_command, CMD_STOP_ACCEPTING_PASSIVE_SVC_CHECKS);

	core_command = command_create("ENABLE_PASSIVE_SVC_CHECKS", service_command_handler,
			 "Enables passive checks for the specified service.", "service=service");
	command_register(core_command, CMD_ENABLE_PASSIVE_SVC_CHECKS);

	core_command = command_create("DISABLE_PASSIVE_SVC_CHECKS", service_command_handler,
			 "Disables passive checks for the specified service.", "service=service");
	command_register(core_command, CMD_DISABLE_PASSIVE_SVC_CHECKS);

	core_command = command_create("SEND_CUSTOM_HOST_NOTIFICATION", host_command_handler,
			"Allows you to send a custom host notification. Very useful in dire situations, emergencies or to communicate with all admins that are responsible for a particular host. When the host notification is sent out, the $NOTIFICATIONTYPE$ macro will be set to 'CUSTOM'. The <options> field is a logical OR of the following integer values that affect aspects of the notification that are sent out: 0 = No option (default), 1 = Broadcast (send notification to all normal and all escalated contacts for the host), 2 = Forced (notification is sent out regardless of current time, whether or not notifications are enabled, etc.), 4 = Increment current notification # for the host (this is not done by default for custom notifications). The comment field can be used with the $NOTIFICATIONCOMMENT$ macro in notification commands.", "host=host_name;int=options;str=author;str=comment");
	command_register(core_command, CMD_SEND_CUSTOM_HOST_NOTIFICATION);

	core_command = command_create("SEND_CUSTOM_SVC_NOTIFICATION", service_command_handler,
			"Allows you to send a custom service notification. Very useful in dire situations, emergencies or to communicate with all admins that are responsible for a particular service. When the service notification is sent out, the $NOTIFICATIONTYPE$ macro will be set to 'CUSTOM'. The <options> field is a logical OR of the following integer values that affect aspects of the notification that are sent out: 0 = No option (default), 1 = Broadcast (send notification to all normal and all escalated contacts for the service), 2 = Forced (notification is sent out regardless of current time, whether or not notifications are enabled, etc.), 4 = Increment current notification # for the service(this is not done by default for custom notifications)", "service=service;int=options;str=author;str=comment");
	command_register(core_command, CMD_SEND_CUSTOM_SVC_NOTIFICATION);

	core_command = command_create("CHANGE_HOST_NOTIFICATION_TIMEPERIOD", service_command_handler,
			"Changes the host notification timeperiod to what is specified by the 'notification_timeperiod' option. The 'notification_timeperiod' option should be the short name of the timeperiod that is to be used as the service notification timeperiod. The timeperiod must have been configured in Naemon before it was last (re)started.", "host=host_name;timeperiod=notification_timeperiod");
	command_register(core_command, CMD_CHANGE_HOST_NOTIFICATION_TIMEPERIOD);

	core_command = command_create("CHANGE_SVC_NOTIFICATION_TIMEPERIOD", service_command_handler,
			"Changes the service notification timeperiod to what is specified by the 'notification_timeperiod' option. The 'notification_timeperiod' option should be the short name of the timeperiod that is to be used as the service notification timeperiod. The timeperiod must have been configured in Naemon before it was last (re)started.", "service=service;timeperiod=notification_timeperiod");
	command_register(core_command, CMD_CHANGE_SVC_NOTIFICATION_TIMEPERIOD);

	core_command = command_create("CHANGE_CONTACT_HOST_NOTIFICATION_TIMEPERIOD", contact_command_handler,
			"Changes the host notification timeperiod for a particular contact to what is specified by the 'notification_timeperiod' option. The 'notification_timeperiod' option should be the short name of the timeperiod that is to be used as the contact's host notification timeperiod. The timeperiod must have been configured in Naemon before it was last (re)started.", "contact=contact_name;timeperiod=notification_timeperiod");
	command_register(core_command, CMD_CHANGE_CONTACT_HOST_NOTIFICATION_TIMEPERIOD);

	core_command = command_create("CHANGE_CONTACT_SVC_NOTIFICATION_TIMEPERIOD", contact_command_handler,
			"Changes the service notification timeperiod for a particular contact to what is specified by the 'notification_timeperiod' option. The 'notification_timeperiod' option should be the short name of the timeperiod that is to be used as the contact's service notification timeperiod. The timeperiod must have been configured in Naemon before it was last (re)started.", "contact=contact_name;timeperiod=notification_timeperiod");
	command_register(core_command, CMD_CHANGE_CONTACT_SVC_NOTIFICATION_TIMEPERIOD);

	core_command = command_create("CHANGE_HOST_MODATTR", host_command_handler,
			"This command changes the modified attributes value for the specified host. Modified attributes values are used by Naemon to determine which object properties should be retained across program restarts. Thus, modifying the value of the attributes can affect data retention. This is an advanced option and should only be used by people who are intimately familiar with the data retention logic in Naemon.", "host=host_name;ulong=value");
	command_register(core_command, CMD_CHANGE_HOST_MODATTR);

	core_command = command_create("CHANGE_SVC_MODATTR", service_command_handler,
			"This command changes the modified attributes value for the specified service. Modified attributes values are used by Naemon to determine which object properties should be retained across program restarts. Thus, modifying the value of the attributes can affect data retention. This is an advanced option and should only be used by people who are intimately familiar with the data retention logic in Naemon.", "host=host_name;service=service;ulong=value");
	command_register(core_command, CMD_CHANGE_SVC_MODATTR);

	core_command = command_create("CHANGE_CONTACT_MODATTR", contact_command_handler,
			"This command changes the modified attributes value for the specified contact. Modified attributes values are used by Naemon to determine which object properties should be retained across program restarts. Thus, modifying the value of the attributes can affect data retention. This is an advanced option and should only be used by people who are intimately familiar with the data retention logic in Naemon.", "contact=contact_name;ulong=value");
	command_register(core_command, CMD_CHANGE_CONTACT_MODATTR);

	core_command = command_create("CHANGE_CONTACT_MODHATTR", contact_command_handler,
			"This command changes the modified host attributes value for the specified contact. Modified attributes values are used by Naemon to determine which object properties should be retained across program restarts. Thus, modifying the value of the attributes can affect data retention. This is an advanced option and should only be used by people who are intimately familiar with the data retention logic in Naemon.", "contact=contact_name;ulong=value");
	command_register(core_command, CMD_CHANGE_CONTACT_MODHATTR);

	core_command = command_create("CHANGE_CONTACT_MODSATTR", contact_command_handler,
			"This command changes the modified service attributes value for the specified contact. Modified attributes values are used by Naemon to determine which object properties should be retained across program restarts. Thus, modifying the value of the attributes can affect data retention. This is an advanced option and should only be used by people who are intimately familiar with the data retention logic in Naemon.", "contact=contact_name;ulong=value");
	command_register(core_command, CMD_CHANGE_CONTACT_MODSATTR);

	core_command = command_create("DEL_DOWNTIME_BY_HOST_NAME", del_downtime_by_filter_handler,
			"This command deletes all downtimes matching the specified filters.", NULL);
	command_argument_add(core_command, "hostname", STRING, "", NULL);
	command_argument_add(core_command, "service_description", STRING, "", NULL);
	command_argument_add(core_command, "downtime_start_time", TIMESTAMP, &default_timestamp, NULL);
	command_argument_add(core_command, "comment", STRING, "", NULL);
	command_register(core_command, CMD_DEL_DOWNTIME_BY_HOST_NAME);

	core_command = command_create("DEL_DOWNTIME_BY_HOSTGROUP_NAME", del_downtime_by_filter_handler,
			"This command deletes all downtimes matching the specified filters.", NULL);
	command_argument_add(core_command, "hostgroup_name", HOSTGROUP, NULL, NULL);
	command_argument_add(core_command, "hostname", STRING, "", NULL);
	command_argument_add(core_command, "service_description", STRING, "", NULL);
	command_argument_add(core_command, "downtime_start_time", TIMESTAMP, &default_timestamp, NULL);
	command_argument_add(core_command, "comment", STRING, "", NULL);
	command_register(core_command, CMD_DEL_DOWNTIME_BY_HOSTGROUP_NAME);

	core_command = command_create("DEL_DOWNTIME_BY_START_TIME_COMMENT", del_downtime_by_filter_handler,
			"", NULL);
	command_argument_add(core_command, "downtime_start_time", TIMESTAMP, &default_timestamp, NULL);
	command_argument_add(core_command, "comment", STRING, "", NULL);
	command_register(core_command, CMD_DEL_DOWNTIME_BY_START_TIME_COMMENT);

	core_command = command_create("ENABLE_EVENT_HANDLERS", global_command_handler,
			"Enables host and service event handlers on a program-wide basis.", NULL);
	command_register(core_command, CMD_ENABLE_EVENT_HANDLERS);

	core_command = command_create("DISABLE_EVENT_HANDLERS", global_command_handler,
			"Disables host and service event handlers on a program-wide basis.", NULL);
	command_register(core_command, CMD_DISABLE_EVENT_HANDLERS);

	core_command = command_create("ENABLE_HOST_EVENT_HANDLER", host_command_handler,
			"Enables the event handler for the specified host.", "host=host_name");
	command_register(core_command, CMD_ENABLE_HOST_EVENT_HANDLER);

	core_command = command_create("DISABLE_HOST_EVENT_HANDLER", host_command_handler,
			"Disables the event handler for the specified host.", "host=host_name");
	command_register(core_command, CMD_DISABLE_HOST_EVENT_HANDLER);

	core_command = command_create("ENABLE_SVC_EVENT_HANDLER", service_command_handler,
			"Enables the event handler for the specified service.", "service=service");
	command_register(core_command, CMD_ENABLE_SVC_EVENT_HANDLER);

	core_command = command_create("DISABLE_SVC_EVENT_HANDLER", service_command_handler,
			"Disables the event handler for the specified service.", "service=service");
	command_register(core_command, CMD_DISABLE_SVC_EVENT_HANDLER);

	core_command = command_create("ENABLE_HOST_CHECK", host_command_handler,
			"Enables (regularly scheduled and on-demand) active checks of the specified host.", "host=host_name");
	command_register(core_command, CMD_ENABLE_HOST_CHECK);

	core_command = command_create("DISABLE_HOST_CHECK", host_command_handler,
			 "Disables (regularly scheduled and on-demand) active checks of the specified host.", "host=host_name");
	command_register(core_command, CMD_DISABLE_HOST_CHECK);

	core_command = command_create("START_OBSESSING_OVER_SVC_CHECKS", global_command_handler,
			"Enables processing of service checks via the OCSP command on a program-wide basis.", NULL);
	command_register(core_command, CMD_START_OBSESSING_OVER_SVC_CHECKS);

	core_command = command_create("STOP_OBSESSING_OVER_SVC_CHECKS", global_command_handler,
			"Disables processing of service checks via the OCSP command on a program-wide basis.", NULL);
	command_register(core_command, CMD_STOP_OBSESSING_OVER_SVC_CHECKS);

	core_command = command_create("REMOVE_HOST_ACKNOWLEDGEMENT", host_command_handler,
			"This removes the problem acknowledgement for a particular host. Once the acknowledgement has been removed, notifications can once again be sent out for the given host.", "host=host_name");
	command_register(core_command, CMD_REMOVE_HOST_ACKNOWLEDGEMENT);

	core_command = command_create("REMOVE_SVC_ACKNOWLEDGEMENT", service_command_handler,
			"This removes the problem acknowledgement for a particular service. Once the acknowledgement has been removed, notifications can once again be sent out for the given service.", "service=service");
	command_register(core_command, CMD_REMOVE_SVC_ACKNOWLEDGEMENT);

	core_command = command_create("SCHEDULE_FORCED_HOST_SVC_CHECKS", host_command_handler,
			"Schedules a forced active check of all services associated with a particular host at 'check_time'. The 'check_time' argument is specified in time_t format (seconds since the UNIX epoch). Forced checks are performed regardless of what time it is (e.g. timeperiod restrictions are ignored) and whether or not active checks are enabled on a service-specific or program-wide basis.", "host=host_name;timestamp=check_time");
	command_register(core_command, CMD_SCHEDULE_FORCED_HOST_SVC_CHECKS);

	core_command = command_create("SCHEDULE_FORCED_SVC_CHECK", service_command_handler,
			"Schedules a forced active check of a particular service at 'check_time'. The 'check_time' argument is specified in time_t format (seconds since the UNIX epoch). Forced checks are performed regardless of what time it is (e.g. timeperiod restrictions are ignored) and whether or not active checks are enabled on a service-specific or program-wide basis.", "service=service;timestamp=check_time");
	command_register(core_command, CMD_SCHEDULE_FORCED_SVC_CHECK);

	core_command = command_create("SCHEDULE_HOST_DOWNTIME", host_command_handler,
 "Schedules downtime for a specified host. If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The specified host downtime can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the specified host should not be triggered by another downtime entry.", "host=host_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_HOST_DOWNTIME);

	core_command = command_create("SCHEDULE_SVC_DOWNTIME", service_command_handler,
			"Schedules downtime for a specified service. If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The specified service downtime can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the specified service should not be triggered by another downtime entry.", "service=service;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_SVC_DOWNTIME);

	core_command = command_create("ENABLE_HOST_FLAP_DETECTION", host_command_handler,
			"Enables flap detection for the specified host.", "host=host_name");
	command_register(core_command, CMD_ENABLE_HOST_FLAP_DETECTION);

	core_command = command_create("DISABLE_HOST_FLAP_DETECTION", host_command_handler,
			"Disables flap detection for the specified host.", "host=host_name");
	command_register(core_command, CMD_DISABLE_HOST_FLAP_DETECTION);

	core_command = command_create("ENABLE_SVC_FLAP_DETECTION", service_command_handler,
			"Enables flap detection for the specified service. In order for the flap detection algorithms to be run for the service, flap detection must be enabled on a program-wide basis as well.", "service=service");
	command_register(core_command, CMD_ENABLE_SVC_FLAP_DETECTION);

	core_command = command_create("DISABLE_SVC_FLAP_DETECTION", service_command_handler,
			"Disables flap detection for the specified service.", "service=service");
	command_register(core_command, CMD_DISABLE_SVC_FLAP_DETECTION);

	core_command = command_create("ENABLE_FLAP_DETECTION", global_command_handler,
			"Enables host and service flap detection on a program-wide basis.", NULL);
	command_register(core_command, CMD_ENABLE_FLAP_DETECTION);

	core_command = command_create("DISABLE_FLAP_DETECTION", global_command_handler,
			"Disables host and service flap detection on a program-wide basis.", NULL);
	command_register(core_command, CMD_DISABLE_FLAP_DETECTION);

	core_command = command_create("ENABLE_HOSTGROUP_SVC_NOTIFICATIONS", hostgroup_command_handler,
			"Enables notifications for all services that are associated with hosts in a particular hostgroup. This does not enable notifications for the hosts in the hostgroup - see the ENABLE_HOSTGROUP_HOST_NOTIFICATIONS command for that. In order for notifications to be sent out for these services, notifications must be enabled on a program-wide basis as well.", "hostgroup=hostgroup_name");

	command_register(core_command, CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS);

	core_command = command_create("DISABLE_HOSTGROUP_SVC_NOTIFICATIONS", hostgroup_command_handler,
			"Disables notifications for all services associated with hosts in a particular hostgroup. This does not disable notifications for the hosts in the hostgroup - see the DISABLE_HOSTGROUP_HOST_NOTIFICATIONS command for that.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS);

	core_command = command_create("ENABLE_HOSTGROUP_HOST_NOTIFICATIONS", hostgroup_command_handler,
			"Enables notifications for all hosts in a particular hostgroup. This does not enable notifications for the services associated with the hosts in the hostgroup - see the ENABLE_HOSTGROUP_SVC_NOTIFICATIONS command for that. In order for notifications to be sent out for these hosts, notifications must be enabled on a program-wide basis as well.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS);

	core_command = command_create("DISABLE_HOSTGROUP_HOST_NOTIFICATIONS", hostgroup_command_handler,
			"Disables notifications for all hosts in a particular hostgroup. This does not disable notifications for the services associated with the hosts in the hostgroup - see the DISABLE_HOSTGROUP_SVC_NOTIFICATIONS command for that.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS);

	core_command = command_create("ENABLE_HOSTGROUP_SVC_CHECKS", hostgroup_command_handler,
			"Enables active checks for all services associated with hosts in a particular hostgroup.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_ENABLE_HOSTGROUP_SVC_CHECKS);

	core_command = command_create("DISABLE_HOSTGROUP_SVC_CHECKS", hostgroup_command_handler,
			"Disables active checks for all services associated with hosts in a particular hostgroup.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_DISABLE_HOSTGROUP_SVC_CHECKS);

	core_command = command_create("DEL_HOST_DOWNTIME", host_command_handler,
			"Deletes the host downtime entry that has an ID number matching the 'downtime_id' argument. If the downtime is currently in effect, the host will come out of scheduled downtime (as long as there are no other overlapping active downtime entries).", "ulong=downtime_id");
	command_register(core_command, CMD_DEL_HOST_DOWNTIME);

	core_command = command_create("DEL_SVC_DOWNTIME", service_command_handler,
			"Deletes the service downtime entry that has an ID number matching the 'downtime_id' argument. If the downtime is currently in effect, the service will come out of scheduled downtime (as long as there are no other overlapping active downtime entries).", "ulong=downtime_id");
	command_register(core_command, CMD_DEL_SVC_DOWNTIME);

	core_command = command_create("ENABLE_PERFORMANCE_DATA", global_command_handler,
			"Enables the processing of host and service performance data on a program-wide basis.", NULL);
	command_register(core_command, CMD_ENABLE_PERFORMANCE_DATA);

	core_command = command_create("DISABLE_PERFORMANCE_DATA", global_command_handler,
			"Disables the processing of host and service performance data on a program-wide basis.", NULL);
	command_register(core_command, CMD_DISABLE_PERFORMANCE_DATA);

	core_command = command_create("SCHEDULE_HOSTGROUP_HOST_DOWNTIME", hostgroup_command_handler,
			"Schedules downtime for all hosts in a specified hostgroup. If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The host downtime entries can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the hosts should not be triggered by another downtime entry.",
			"hostgroup=hostgroup_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME);

	core_command = command_create("SCHEDULE_HOSTGROUP_SVC_DOWNTIME", hostgroup_command_handler,
			"Schedules downtime for all services associated with hosts in a specified servicegroup. If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The service downtime entries can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the services should not be triggered by another downtime entry.",
			"hostgroup=hostgroup_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME);

	core_command = command_create("SCHEDULE_HOST_SVC_DOWNTIME", host_command_handler,
			"Schedules downtime for all services associated with a particular host. If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The service downtime entries can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the services should not be triggered by another downtime entry.",
			"host=host_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_HOST_SVC_DOWNTIME);

	core_command = command_create("PROCESS_HOST_CHECK_RESULT", host_command_handler,
			"This is used to submit a passive check result for a particular host. The 'status_code' indicates the state of the host check and should be one of the following: 0=UP, 1=DOWN, 2=UNREACHABLE. The 'plugin_output' argument contains the text returned from the host check, along with optional performance data.",
			"host=host_name;int=status_code;str=plugin_output");
	command_register(core_command, CMD_PROCESS_HOST_CHECK_RESULT);

	core_command = command_create("START_EXECUTING_HOST_CHECKS", global_command_handler,
			"Enables active host checks on a program-wide basis.", NULL);
	command_register(core_command, CMD_START_EXECUTING_HOST_CHECKS);

	core_command = command_create("STOP_EXECUTING_HOST_CHECKS", global_command_handler,
			"Disables active host checks on a program-wide basis.", NULL);
	command_register(core_command, CMD_STOP_EXECUTING_HOST_CHECKS);

	core_command = command_create("START_ACCEPTING_PASSIVE_HOST_CHECKS", global_command_handler,
			"Enables acceptance and processing of passive host checks on a program-wide basis.", NULL);
	command_register(core_command, CMD_START_ACCEPTING_PASSIVE_HOST_CHECKS);

	core_command = command_create("STOP_ACCEPTING_PASSIVE_HOST_CHECKS", global_command_handler,
			"Disables acceptance and processing of passive host checks on a program-wide basis.", NULL);
	command_register(core_command, CMD_STOP_ACCEPTING_PASSIVE_HOST_CHECKS);

	core_command = command_create("ENABLE_PASSIVE_HOST_CHECKS", host_command_handler,
			"Enables acceptance and processing of passive host checks for the specified host.", "host=host_name");
	command_register(core_command, CMD_ENABLE_PASSIVE_HOST_CHECKS);

	core_command = command_create("DISABLE_PASSIVE_HOST_CHECKS", host_command_handler,
			"Disables acceptance and processing of passive host checks for the specified host.", "host=host_name");
	command_register(core_command, CMD_DISABLE_PASSIVE_HOST_CHECKS);

	core_command = command_create("START_OBSESSING_OVER_HOST_CHECKS", global_command_handler,
			"Enables processing of host checks via the OCHP command on a program-wide basis.", NULL);
	command_register(core_command, CMD_START_OBSESSING_OVER_HOST_CHECKS);

	core_command = command_create("STOP_OBSESSING_OVER_HOST_CHECKS", global_command_handler,
			"Disables processing of host checks via the OCHP command on a program-wide basis.", NULL);
	command_register(core_command, CMD_STOP_OBSESSING_OVER_HOST_CHECKS);

	core_command = command_create("SCHEDULE_HOST_CHECK", host_command_handler,
			"Schedules the next active check of a particular host at 'check_time'. The 'check_time' argument is specified in time_t format (seconds since the UNIX epoch). Note that the host may not actually be checked at the time you specify. This could occur for a number of reasons: active checks are disabled on a program-wide or service-specific basis, the host is already scheduled to be checked at an earlier time, etc. If you want to force the host check to occur at the time you specify, look at the SCHEDULE_FORCED_HOST_CHECK command.", "host=host_name;timestamp=check_time");
	command_register(core_command, CMD_SCHEDULE_HOST_CHECK);

	core_command = command_create("SCHEDULE_FORCED_HOST_CHECK", host_command_handler,
			"Schedules a forced active check of a particular host at 'check_time'. The 'check_time' argument is specified in time_t format (seconds since the UNIX epoch). Forced checks are performed regardless of what time it is (e.g. timeperiod restrictions are ignored) and whether or not active checks are enabled on a host-specific or program-wide basis.", "host=host_name;timestamp=check_time");
	command_register(core_command, CMD_SCHEDULE_FORCED_HOST_CHECK);

	core_command = command_create("START_OBSESSING_OVER_SVC", service_command_handler,
			"Enables processing of service checks via the OCSP command for the specified service.", "service=service");
	command_register(core_command, CMD_START_OBSESSING_OVER_SVC);

	core_command = command_create("STOP_OBSESSING_OVER_SVC", service_command_handler,
			"Disables processing of service checks via the OCSP command for the specified service.", "service=service");
	command_register(core_command, CMD_STOP_OBSESSING_OVER_SVC);

	core_command = command_create("START_OBSESSING_OVER_HOST", host_command_handler,
			"Enables processing of host checks via the OCHP command for the specified host.", "host=host_name");
	command_register(core_command, CMD_START_OBSESSING_OVER_HOST);

	core_command = command_create("STOP_OBSESSING_OVER_HOST", host_command_handler,
			"Disables processing of host checks via the OCHP command for the specified host.", "host=host_name");
	command_register(core_command, CMD_STOP_OBSESSING_OVER_HOST);

	core_command = command_create("ENABLE_HOSTGROUP_HOST_CHECKS", hostgroup_command_handler,
			"Enables active checks for all hosts in a particular hostgroup.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_ENABLE_HOSTGROUP_HOST_CHECKS);

	core_command = command_create("DISABLE_HOSTGROUP_HOST_CHECKS", hostgroup_command_handler,
			"Disables active checks for all hosts in a particular hostgroup.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_DISABLE_HOSTGROUP_HOST_CHECKS);

	core_command = command_create("ENABLE_HOSTGROUP_PASSIVE_SVC_CHECKS", hostgroup_command_handler,
			"Enables passive checks for all services associated with hosts in a particular hostgroup.", "hostgroup=hosgroup_name");
	command_register(core_command, CMD_ENABLE_HOSTGROUP_PASSIVE_SVC_CHECKS);

	core_command = command_create("DISABLE_HOSTGROUP_PASSIVE_SVC_CHECKS", hostgroup_command_handler,
			"Disables passive checks for all services associated with hosts in a particular hostgroup.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_DISABLE_HOSTGROUP_PASSIVE_SVC_CHECKS);

	core_command = command_create("ENABLE_HOSTGROUP_PASSIVE_HOST_CHECKS", hostgroup_command_handler,
			"Enables passive checks for all hosts in a particular hostgroup.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_ENABLE_HOSTGROUP_PASSIVE_HOST_CHECKS);

	core_command = command_create("DISABLE_HOSTGROUP_PASSIVE_HOST_CHECKS", hostgroup_command_handler,
			"Disables passive checks for all hosts in a particular hostgroup.", "hostgroup=hostgroup_name");
	command_register(core_command, CMD_DISABLE_HOSTGROUP_PASSIVE_HOST_CHECKS);

	core_command = command_create("ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS", servicegroup_command_handler,
			"Enables notifications for all services that are members of a particular servicegroup. In order for notifications to be sent out for these services, notifications must also be enabled on a program-wide basis.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS);

	core_command = command_create("DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS", servicegroup_command_handler,
			"Disables notifications for all services that are members of a particular servicegroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS);

	core_command = command_create("ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS", servicegroup_command_handler,
			"Enables notifications for all hosts that have services that are members of a particular servicegroup. In order for notifications to be sent out for these hosts, notifications must also be enabled on a program-wide basis.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS);

	core_command = command_create("DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS", servicegroup_command_handler,
			"Disables notifications for all hosts that have services that are members of a particular servicegroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS);

	core_command = command_create("ENABLE_SERVICEGROUP_SVC_CHECKS", servicegroup_command_handler,
			"Enables active checks for all services in a particular servicegroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_ENABLE_SERVICEGROUP_SVC_CHECKS);

	core_command = command_create("DISABLE_SERVICEGROUP_SVC_CHECKS", servicegroup_command_handler,
			"Disables active checks for all services in a particular servicegroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_DISABLE_SERVICEGROUP_SVC_CHECKS);

	core_command = command_create("ENABLE_SERVICEGROUP_HOST_CHECKS", servicegroup_command_handler,
			"Enables active checks for all hosts that have services that are members of a particular hostgroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_ENABLE_SERVICEGROUP_HOST_CHECKS);

	core_command = command_create("DISABLE_SERVICEGROUP_HOST_CHECKS", servicegroup_command_handler,
			"Disables active checks for all hosts that have services that are members of a particular hostgroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_DISABLE_SERVICEGROUP_HOST_CHECKS);

	core_command = command_create("ENABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS", servicegroup_command_handler,
			"Enables the acceptance and processing of passive checks for all services in a particular servicegroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_ENABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS);

	core_command = command_create("DISABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS", servicegroup_command_handler,
			"Disables the acceptance and processing of passive checks for all services in a particular servicegroup.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_DISABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS);

	core_command = command_create("ENABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS", servicegroup_command_handler,
			"Enables the acceptance and processing of passive checks for all hosts that have services that are members of a particular service group.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_ENABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS);

	 core_command = command_create("DISABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS", servicegroup_command_handler,
			"Disables the acceptance and processing of passive checks for all hosts that have services that are members of a particular service group.", "servicegroup=servicegroup_name");
	command_register(core_command, CMD_DISABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS);

	 core_command = command_create("SCHEDULE_SERVICEGROUP_HOST_DOWNTIME", servicegroup_command_handler,
			"Schedules downtime for all hosts that have services in a specified servicegroup. If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The host downtime entries can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the hosts should not be triggered by another downtime entry.",
			"servicegroup=servicegroup_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME);

	core_command = command_create("SCHEDULE_SERVICEGROUP_SVC_DOWNTIME", servicegroup_command_handler,
			"Schedules downtime for all services in a specified servicegroup. If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The service downtime entries can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the services should not be triggered by another downtime entry.",
			"servicegroup=servicegroup_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME);

	core_command = command_create("CHANGE_GLOBAL_HOST_EVENT_HANDLER", global_command_handler,
			"Changes the global host event handler command to be that specified by the 'event_handler_command' option. The 'event_handler_command' option specifies the short name of the command that should be used as the new host event handler. The command must have been configured in Naemon before it was last (re)started.", "str=event_handler_command");
	command_register(core_command, CMD_CHANGE_GLOBAL_HOST_EVENT_HANDLER);

	core_command = command_create("CHANGE_GLOBAL_SVC_EVENT_HANDLER", global_command_handler,
			"Changes the global service event handler command to be that specified by the 'event_handler_command' option. The 'event_handler_command' option specifies the short name of the command that should be used as the new service event handler. The command must have been configured in Naemon before it was last (re)started.", "str=event_handler_command");
	command_register(core_command, CMD_CHANGE_GLOBAL_SVC_EVENT_HANDLER);

	core_command = command_create("CHANGE_HOST_EVENT_HANDLER", host_command_handler,
			"Changes the event handler command for a particular host to be that specified by the 'event_handler_command' option. The 'event_handler_command' option specifies the short name of the command that should be used as the new host event handler. The command must have been configured in Naemon before it was last (re)started.", "host=host_name;str=event_handler_command");
	command_register(core_command, CMD_CHANGE_HOST_EVENT_HANDLER);

	core_command = command_create("CHANGE_SVC_EVENT_HANDLER", service_command_handler,
			"Changes the event handler command for a particular service to be that specified by the 'event_handler_command' option. The 'event_handler_command' option specifies the short name of the command that should be used as the new service event handler. The command must have been configured in Naemon before it was last (re)started.", "service=service;str=event_handler_command");
	command_register(core_command, CMD_CHANGE_SVC_EVENT_HANDLER);

	core_command = command_create("CHANGE_HOST_CHECK_COMMAND", host_command_handler,
			"Changes the check command for a particular host to be that specified by the 'check_command' option. The 'check_command' option specifies the short name of the command that should be used as the new host check command. The command must have been configured in Naemon before it was last (re)started.", "host=host_name;str=check_command");
	command_register(core_command, CMD_CHANGE_HOST_CHECK_COMMAND);

	core_command = command_create("CHANGE_SVC_CHECK_COMMAND", service_command_handler,
			"Changes the check command for a particular service to be that specified by the 'check_command' option. The 'check_command' option specifies the short name of the command that should be used as the new service check command. The command must have been configured in Naemon before it was last (re)started.", "service=service;str=check_command");
	command_register(core_command, CMD_CHANGE_SVC_CHECK_COMMAND);

	core_command = command_create("CHANGE_NORMAL_HOST_CHECK_INTERVAL", host_command_handler,
			"Changes the normal (regularly scheduled) check interval for a particular host.", "host=host_name;timestamp=check_interval");
	command_register(core_command, CMD_CHANGE_NORMAL_HOST_CHECK_INTERVAL);

	core_command = command_create("CHANGE_NORMAL_SVC_CHECK_INTERVAL", service_command_handler,
			"Changes the normal (regularly scheduled) check interval for a particular service", "service=service;timestamp=check_interval");
	command_register(core_command, CMD_CHANGE_NORMAL_SVC_CHECK_INTERVAL);

	core_command = command_create("CHANGE_RETRY_SVC_CHECK_INTERVAL", service_command_handler,
			"Changes the retry check interval for a particular service.", "service=service;timestamp=check_interval");
	command_register(core_command, CMD_CHANGE_RETRY_SVC_CHECK_INTERVAL);

	core_command = command_create("CHANGE_MAX_HOST_CHECK_ATTEMPTS", host_command_handler,
			"Changes the maximum number of check attempts (retries) for a particular host.", "host=host_name;int=check_attempts");
	command_register(core_command, CMD_CHANGE_MAX_HOST_CHECK_ATTEMPTS);

	core_command = command_create("CHANGE_MAX_SVC_CHECK_ATTEMPTS", service_command_handler,
			"Changes the maximum number of check attempts (retries) for a particular service.", "service=service;int=check_attempts");
	command_register(core_command, CMD_CHANGE_MAX_SVC_CHECK_ATTEMPTS);

	core_command = command_create("SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME", host_command_handler,
			"Schedules downtime for a specified host and all of its children (hosts). If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). Downtime for child hosts are all set to be triggered by the downtime for the specified (parent) host. The specified (parent) host downtime can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the specified (parent) host should not be triggered by another downtime entry.",
			"host=host_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME);

	core_command = command_create("ENABLE_HOST_AND_CHILD_NOTIFICATIONS", host_command_handler,
			"Enables notifications for the specified host, as well as all hosts 'beyond' (e.g. on all child hosts of) the specified host. Notifications will only be sent out for these hosts if notifications are also enabled on a program-wide basis.", "host=host_name");
	command_register(core_command, CMD_ENABLE_HOST_AND_CHILD_NOTIFICATIONS);

	core_command = command_create("DISABLE_HOST_AND_CHILD_NOTIFICATIONS", host_command_handler,
			"Disables notifications for the specified host, as well as all hosts 'beyond' (e.g. on all child hosts of) the specified host.", "host=host_name");
	command_register(core_command, CMD_DISABLE_HOST_AND_CHILD_NOTIFICATIONS);

	core_command = command_create("SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME", host_command_handler,
			"Schedules downtime for a specified host and all of its children (hosts). If the 'fixed' argument is set to one (1), downtime will start and end at the times specified by the 'start' and 'end' arguments. Otherwise, downtime will begin between the 'start' and 'end' times and last for 'duration' seconds. The 'start' and 'end' arguments are specified in time_t format (seconds since the UNIX epoch). The specified (parent) host downtime can be triggered by another downtime entry if the 'trigger_id' is set to the ID of another scheduled downtime entry. Set the 'trigger_id' argument to zero (0) if the downtime for the specified (parent) host should not be triggered by another downtime entry.",
			"host=host_name;timestamp=start_time;timestamp=end_time;bool=fixed;ulong=trigger_id;ulong=duration;str=author;str=comment");
	command_register(core_command, CMD_SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME);

	core_command = command_create("ENABLE_SERVICE_FRESHNESS_CHECKS", global_command_handler,
			"Enables freshness checks of all services on a program-wide basis. Individual services that have freshness checks disabled will not be checked for freshness.", NULL);
	command_register(core_command, CMD_ENABLE_SERVICE_FRESHNESS_CHECKS);

	core_command = command_create("DISABLE_SERVICE_FRESHNESS_CHECKS", global_command_handler,
			"Disables freshness checks of all services on a program-wide basis.", NULL);
	command_register(core_command, CMD_DISABLE_SERVICE_FRESHNESS_CHECKS);

	core_command = command_create("ENABLE_HOST_FRESHNESS_CHECKS", global_command_handler,
			"Enables freshness checks of all hosts on a program-wide basis. Individual hosts that have freshness checks disabled will not be checked for freshness.", NULL);
	command_register(core_command, CMD_ENABLE_HOST_FRESHNESS_CHECKS);

	core_command = command_create("DISABLE_HOST_FRESHNESS_CHECKS", global_command_handler,
			"Disables freshness checks of all hosts on a program-wide basis.", NULL);
	command_register(core_command, CMD_DISABLE_HOST_FRESHNESS_CHECKS);

	core_command = command_create("SET_HOST_NOTIFICATION_NUMBER", host_command_handler,
			" Sets the current notification number for a particular host. A value of 0 indicates that no notification has yet been sent for the current host problem. Useful for forcing an escalation (based on notification number) or replicating notification information in redundant monitoring environments. Notification numbers greater than zero have no noticeable affect on the notification process if the host is currently in an UP state.", "host=host_name;int=notification_number");
	command_register(core_command, CMD_SET_HOST_NOTIFICATION_NUMBER);

	core_command = command_create("SET_SVC_NOTIFICATION_NUMBER", service_command_handler,
			"Sets the current notification number for a particular service. A value of 0 indicates that no notification has yet been sent for the current service problem. Useful for forcing an escalation (based on notification number) or replicating notification information in redundant monitoring environments. Notification numbers greater than zero have no noticeable affect on the notification process if the service is currently in an OK state.", "service=service;int=notification_number");
	command_register(core_command, CMD_SET_SVC_NOTIFICATION_NUMBER);

	core_command = command_create("CHANGE_HOST_CHECK_TIMEPERIOD", host_command_handler,
			"Changes the valid check period for the specified host.", "host=host_name;timeperiod=timeperiod");
	command_register(core_command, CMD_CHANGE_HOST_CHECK_TIMEPERIOD);

	core_command = command_create("CHANGE_SVC_CHECK_TIMEPERIOD", service_command_handler,
			"Changes the check timeperiod for a particular service to what is specified by the 'check_timeperiod' option. The 'check_timeperiod' option should be the short name of the timeperod that is to be used as the service check timeperiod. The timeperiod must have been configured in Naemon before it was last (re)started.",
			"service=service;timeperiod=check_timeperiod");
	command_register(core_command, CMD_CHANGE_SVC_CHECK_TIMEPERIOD);

	core_command = command_create("PROCESS_FILE", global_command_handler,
			"Directs Naemon to process all external commands that are found in the file specified by the <file_name> argument. If the <delete> option is non-zero, the file will be deleted once it has been processes. If the <delete> option is set to zero, the file is left untouched.", "str=file_name;bool=delete");
	command_register(core_command, CMD_PROCESS_FILE);

	core_command = command_create("CHANGE_CUSTOM_HOST_VAR", change_custom_var_handler,
			"Changes the value of a custom host variable.", "host=host_name;str=varname;str=varvalue");
	command_register(core_command, CMD_CHANGE_CUSTOM_HOST_VAR);

	core_command = command_create("CHANGE_CUSTOM_SVC_VAR", change_custom_var_handler,
			"Changes the value of a custom service variable.", "service=service;str=varname;str=varvalue");
	command_register(core_command, CMD_CHANGE_CUSTOM_SVC_VAR);

	core_command = command_create("CHANGE_CUSTOM_CONTACT_VAR", change_custom_var_handler,
			"Changes the value of a custom contact variable.", "contact=contact_name;str=varname;str=varvalue");
	command_register(core_command, CMD_CHANGE_CUSTOM_CONTACT_VAR);

	core_command = command_create("ENABLE_CONTACT_HOST_NOTIFICATIONS", contact_command_handler,
			"Enables host notifications for a particular contact.", "contact=contact_name");
	command_register(core_command, CMD_ENABLE_CONTACT_HOST_NOTIFICATIONS);

	core_command = command_create("DISABLE_CONTACT_HOST_NOTIFICATIONS", contact_command_handler,
			"Disables host notifications for a particular contact.", "contact=contact_name");
	command_register(core_command, CMD_DISABLE_CONTACT_HOST_NOTIFICATIONS);

	core_command = command_create("ENABLE_CONTACT_SVC_NOTIFICATIONS", contact_command_handler,
			"Disables service notifications for a particular contact.", "contact=contact_name");
	command_register(core_command, CMD_ENABLE_CONTACT_SVC_NOTIFICATIONS);

	core_command = command_create("DISABLE_CONTACT_SVC_NOTIFICATIONS", contact_command_handler,
			"Disables service notifications for a particular contact.", "contact=contact_name");
	command_register(core_command, CMD_DISABLE_CONTACT_SVC_NOTIFICATIONS);

	core_command = command_create("ENABLE_CONTACTGROUP_HOST_NOTIFICATIONS", contactgroup_command_handler,
			"Enables host notifications for all contacts in a particular contactgroup.", "contactgroup=contactgroup_name");
	command_register(core_command, CMD_ENABLE_CONTACTGROUP_HOST_NOTIFICATIONS);

	core_command = command_create("DISABLE_CONTACTGROUP_HOST_NOTIFICATIONS", contactgroup_command_handler,
			"Disables host notifications for all contacts in a particular contactgroup.", "contactgroup=contactgroup_name");
	command_register(core_command, CMD_DISABLE_CONTACTGROUP_HOST_NOTIFICATIONS);

	core_command = command_create("ENABLE_CONTACTGROUP_SVC_NOTIFICATIONS", contactgroup_command_handler,
			"Enables service notifications for all contacts in a particular contactgroup.", "contactgroup=contactgroup_name");
	command_register(core_command, CMD_ENABLE_CONTACTGROUP_SVC_NOTIFICATIONS);

	core_command = command_create("DISABLE_CONTACTGROUP_SVC_NOTIFICATIONS", contactgroup_command_handler,
			"Disables service notifications for all contacts in a particular contactgroup.", "contactgroup=contactgroup_name");
	command_register(core_command, CMD_DISABLE_CONTACTGROUP_SVC_NOTIFICATIONS);

	core_command = command_create("CHANGE_RETRY_HOST_CHECK_INTERVAL", host_command_handler,
			"Changes the retry check interval for a particular host.", "host=host_name;timestamp=check_interval");
	command_register(core_command, CMD_CHANGE_RETRY_HOST_CHECK_INTERVAL);
}

/******************************************************************/
/****************** EXTERNAL COMMAND PROCESSING *******************/
/******************************************************************/

/*** stupid helpers ****/
static host *find_host_by_name_or_address(const char *name)
{
	host *h;

	if ((h = find_host(name)) || !name)
		return h;

	for (h = host_list; h; h = h->next)
		if (!strcmp(h->address, name))
			return h;

	return NULL;
}

/* processes all external commands in a (regular) file */
int process_external_commands_from_file(char *fname, int delete_file)
{
	mmapfile *thefile = NULL;
	char *input = NULL;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "process_external_commands_from_file()\n");

	if (fname == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EXTERNALCOMMANDS, 1, "Processing commands from file '%s'.  File will %s deleted after processing.\n", fname, (delete_file == TRUE) ? "be" : "NOT be");

	/* open the config file for reading */
	if ((thefile = mmap_fopen(fname)) == NULL) {
		logit(NSLOG_INFO_MESSAGE, FALSE, "Error: Cannot open file '%s' to process external commands!", fname);
		return ERROR;
	}

	/* process all commands in the file */
	while (1) {

		/* free memory */
		my_free(input);

		/* read the next line */
		if ((input = mmap_fgets(thefile)) == NULL)
			break;

		/* process the command */
		process_external_command1(input);
	}

	/* close the file */
	mmap_fclose(thefile);

	/* delete the file */
	if (delete_file == TRUE)
		unlink(fname);

	return OK;
}


/* top-level external command processor */
int process_external_command1(char *cmd)
{
	char *temp_buffer = NULL;
	char *args = NULL;
	char *name = NULL;
	int id = CMD_NONE;
	int external_command_ret = OK;
	struct external_command *parsed_command;

	log_debug_info(DEBUGL_FUNCTIONS, 0, "process_external_command1()\n");

	if (cmd == NULL)
		return CMD_ERROR_MALFORMED_COMMAND;

	/* strip the command of newlines and carriage returns */
	strip(cmd);

	log_debug_info(DEBUGL_EXTERNALCOMMANDS, 2, "Raw command entry: %s\n", cmd);
	/* is the command in the command register? */
	parsed_command = command_parse(cmd, COMMAND_SYNTAX_NOKV, &external_command_ret);
	if ( external_command_ret == CMD_ERROR_CUSTOM_COMMAND ) {
		id = CMD_CUSTOM_COMMAND;
		/*custom command, reset return value*/
		external_command_ret = CMD_ERROR_OK;
	}
	else if (external_command_ret != CMD_ERROR_OK) {
		logit(NSLOG_EXTERNAL_COMMAND | NSLOG_RUNTIME_WARNING, TRUE, "Warning: External command parse error %s (%s)\n", cmd, cmd_error_strerror(external_command_ret));
		return external_command_ret;
	}
	else {
		id = command_id(parsed_command);
	}
	/*XXX: retain const correctness, broker_external_command below discards it though it seems to not
	 * modify its arguments*/
	name = strdup(command_name(parsed_command));
	args = strdup(command_raw_arguments(parsed_command));

	/* update statistics for external commands */
	update_check_stats(EXTERNAL_COMMAND_STATS, time(NULL));

	/* log the external command */
	asprintf(&temp_buffer, "EXTERNAL COMMAND: %s;%s\n", name, args);
	if (id == CMD_PROCESS_SERVICE_CHECK_RESULT || id == CMD_PROCESS_HOST_CHECK_RESULT) {
		/* passive checks are logged in checks.c as well, as some my bypass external commands by getting dropped in checkresults dir */
		if (log_passive_checks == TRUE)
			write_to_all_logs(temp_buffer, NSLOG_PASSIVE_CHECK);
	} else if (log_external_commands == TRUE) {
			write_to_all_logs(temp_buffer, NSLOG_EXTERNAL_COMMAND);
	}
	my_free(temp_buffer);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_external_command(NEBTYPE_EXTERNALCOMMAND_START, NEBFLAG_NONE, NEBATTR_NONE, id, command_entry_time(parsed_command), name, args, NULL);
#endif

	/* custom commands aren't handled internally by Naemon, but may be by NEB modules */
	if (id != CMD_CUSTOM_COMMAND) {
		external_command_ret = command_execute_handler(parsed_command);
		if (external_command_ret != CMD_ERROR_OK) {
			logit(NSLOG_EXTERNAL_COMMAND | NSLOG_RUNTIME_WARNING, TRUE, "Error: External command failed -> %s;%s (%s)\n", name, args, cmd_error_strerror(external_command_ret));
		}
	}


#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_external_command(NEBTYPE_EXTERNALCOMMAND_END, NEBFLAG_NONE, NEBATTR_NONE, id, command_entry_time(parsed_command), name, args, NULL);
#endif

	/* free memory */
	free(name);
	free(args);
	command_destroy(parsed_command);
	return external_command_ret;
}

int process_external_command2(int cmd, time_t entry_time, char *args)
{
	struct external_command *ext_command = NULL;
	int ret = CMD_ERROR_OK;
	log_debug_info(DEBUGL_FUNCTIONS, 0, "process_external_command1()\n");
	log_debug_info(DEBUGL_EXTERNALCOMMANDS, 1, "External Command Type: %d\n", cmd);
	log_debug_info(DEBUGL_EXTERNALCOMMANDS, 1, "Command Entry Time: %lu\n", (unsigned long)entry_time);
	log_debug_info(DEBUGL_EXTERNALCOMMANDS, 1, "Command Arguments: %s\n", (args == NULL) ? "" : args);

	ext_command = external_command_copy(registered_commands[cmd]);
	ext_command->entry_time = entry_time;
	ret = parse_arguments((const char *)args, ext_command->arguments, ext_command->argc);
	if (ret == CMD_ERROR_OK) {
		ret = command_execute_handler(ext_command);
	}
	command_destroy(ext_command);
	return ret;
}

const char *cmd_error_strerror(int code)
{
	switch (code) {
	case CMD_ERROR_OK:
		return "No error";
	case CMD_ERROR_FAILURE:
	case ERROR:
		return "Command failed";
	case CMD_ERROR_INTERNAL_ERROR:
		return "Internal error";
	case CMD_ERROR_UNKNOWN_COMMAND:
		return "Unknown or unsupported command";
	case CMD_ERROR_MALFORMED_COMMAND:
		return "Malformed command";
	case CMD_ERROR_PARSE_MISSING_ARG:
		return "Missing argument";
	case CMD_ERROR_PARSE_EXCESS_ARG:
		return "Too many arguments";
	case CMD_ERROR_PARSE_TYPE_MISMATCH:
		return "Wrong type for argument";
	case CMD_ERROR_UNSUPPORTED_ARG_TYPE:
		return "Unsupported argument type";
	case CMD_ERROR_VALIDATION_FAILURE:
		return "Invalid value";
	case CMD_ERROR_UNSUPPORTED_PARSE_MODE:
		return "Unsupported parse mode";
	case CMD_ERROR_CUSTOM_COMMAND:
		return "Custom command";
	}
	return "Unknown error";
}

/******************************************************************/
/*************** INTERNAL COMMAND IMPLEMENTATIONS  ****************/
/******************************************************************/

/* submits a passive service check result for later processing */
int process_passive_service_check(time_t check_time, char *host_name, char *svc_description, int return_code, char *output)
{
	check_result cr;
	host *temp_host = NULL;
	service *temp_service = NULL;
	struct timeval tv;

	/* skip this service check result if we aren't accepting passive service checks */
	if (accept_passive_service_checks == FALSE)
		return ERROR;

	/* make sure we have all required data */
	if (host_name == NULL || svc_description == NULL || output == NULL)
		return ERROR;

	temp_host = find_host_by_name_or_address(host_name);

	/* we couldn't find the host */
	if (temp_host == NULL) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning:  Passive check result was received for service '%s' on host '%s', but the host could not be found!\n", svc_description, host_name);
		return ERROR;
	}

	/* make sure the service exists */
	if ((temp_service = find_service(temp_host->name, svc_description)) == NULL) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning:  Passive check result was received for service '%s' on host '%s', but the service could not be found!\n", svc_description, host_name);
		return ERROR;
	}

	/* skip this is we aren't accepting passive checks for this service */
	if (temp_service->accept_passive_checks == FALSE)
		return ERROR;

	memset(&cr, 0, sizeof(cr));
	cr.exited_ok = 1;
	cr.check_type = CHECK_TYPE_PASSIVE;
	cr.host_name = temp_host->name;
	cr.service_description = temp_service->description;
	cr.output = output;
	cr.start_time.tv_sec = cr.finish_time.tv_sec = check_time;
	cr.source = (void*)command_worker.source_name;

	/* save the return code and make sure it's sane */
	cr.return_code = return_code;
	if (cr.return_code < 0 || cr.return_code > 3)
		cr.return_code = STATE_UNKNOWN;

	/* calculate latency */
	gettimeofday(&tv, NULL);
	cr.latency = (double)((double)(tv.tv_sec - check_time) + (double)(tv.tv_usec / 1000.0) / 1000.0);
	if (cr.latency < 0.0)
		cr.latency = 0.0;

	return handle_async_service_check_result(temp_service, &cr);
}

/* process passive host check result */
int process_passive_host_check(time_t check_time, char *host_name, int return_code, char *output)
{
	check_result cr;
	host *temp_host = NULL;
	struct timeval tv;

	/* skip this host check result if we aren't accepting passive host checks */
	if (accept_passive_host_checks == FALSE)
		return ERROR;

	/* make sure we have all required data */
	if (host_name == NULL || output == NULL)
		return ERROR;

	/* make sure we have a reasonable return code */
	if (return_code < 0 || return_code > 2)
		return ERROR;

	/* find the host by its name or address */
	temp_host = find_host_by_name_or_address(host_name);

	/* we couldn't find the host */
	if (temp_host == NULL) {
		logit(NSLOG_RUNTIME_WARNING, TRUE, "Warning:  Passive check result was received for host '%s', but the host could not be found!\n", host_name);
		return ERROR;
	}

	/* skip this is we aren't accepting passive checks for this host */
	if (temp_host->accept_passive_checks == FALSE)
		return ERROR;

	memset(&cr, 0, sizeof(cr));
	cr.exited_ok = 1;
	cr.check_type = CHECK_TYPE_PASSIVE;
	cr.host_name = temp_host->name;
	cr.output = output;
	cr.start_time.tv_sec = cr.finish_time.tv_sec = check_time;
	cr.source = (void*)command_worker.source_name;
	cr.return_code = return_code;

	/* calculate latency */
	gettimeofday(&tv, NULL);
	cr.latency = (double)((double)(tv.tv_sec - check_time) + (double)(tv.tv_usec / 1000.0) / 1000.0);
	if (cr.latency < 0.0)
		cr.latency = 0.0;

	handle_async_host_check_result(temp_host, &cr);

	return OK;
}

/* temporarily disables a service check */
void disable_service_checks(service *svc)
{
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* checks are already disabled */
	if (svc->checks_enabled == FALSE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* disable the service check... */
	svc->checks_enabled = FALSE;
	svc->should_be_scheduled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log to reflect the new service state */
	update_service_status(svc, FALSE);

	return;
}


/* enables a service check */
void enable_service_checks(service *svc)
{
	time_t preferred_time = 0L;
	time_t next_valid_time = 0L;
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* checks are already enabled */
	if (svc->checks_enabled == TRUE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* enable the service check... */
	svc->checks_enabled = TRUE;
	svc->should_be_scheduled = TRUE;

	/* services with no check intervals don't get checked */
	if (svc->check_interval == 0)
		svc->should_be_scheduled = FALSE;

	/* schedule a check for right now (or as soon as possible) */
	time(&preferred_time);
	if (check_time_against_period(preferred_time, svc->check_period_ptr) == ERROR) {
		get_next_valid_time(preferred_time, &next_valid_time, svc->check_period_ptr);
		svc->next_check = next_valid_time;
	} else
		svc->next_check = preferred_time;

	/* schedule a check if we should */
	if (svc->should_be_scheduled == TRUE)
		schedule_service_check(svc, svc->next_check, CHECK_OPTION_NONE);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log to reflect the new service state */
	update_service_status(svc, FALSE);

	return;
}


/* enable notifications on a program-wide basis */
void enable_all_notifications(void)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* bail out if we're already set... */
	if (enable_notifications == TRUE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;
	modified_service_process_attributes |= attr;

	/* update notification status */
	enable_notifications = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log */
	update_program_status(FALSE);

	return;
}


/* disable notifications on a program-wide basis */
void disable_all_notifications(void)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* bail out if we're already set... */
	if (enable_notifications == FALSE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;
	modified_service_process_attributes |= attr;

	/* update notification status */
	enable_notifications = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log */
	update_program_status(FALSE);

	return;
}


/* enables notifications for a service */
void enable_service_notifications(service *svc)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (svc->notifications_enabled == TRUE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* enable the service notifications... */
	svc->notifications_enabled = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log to reflect the new service state */
	update_service_status(svc, FALSE);

	return;
}


/* disables notifications for a service */
void disable_service_notifications(service *svc)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (svc->notifications_enabled == FALSE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* disable the service notifications... */
	svc->notifications_enabled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log to reflect the new service state */
	update_service_status(svc, FALSE);

	return;
}


/* enables notifications for a host */
void enable_host_notifications(host *hst)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (hst->notifications_enabled == TRUE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* enable the host notifications... */
	hst->notifications_enabled = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log to reflect the new host state */
	update_host_status(hst, FALSE);

	return;
}


/* disables notifications for a host */
void disable_host_notifications(host *hst)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (hst->notifications_enabled == FALSE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* disable the host notifications... */
	hst->notifications_enabled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log to reflect the new host state */
	update_host_status(hst, FALSE);

	return;
}


/* enables notifications for all hosts and services "beyond" a given host */
void enable_and_propagate_notifications(host *hst, int level, int affect_top_host, int affect_hosts, int affect_services)
{
	host *child_host = NULL;
	service *temp_service = NULL;
	servicesmember *temp_servicesmember = NULL;
	hostsmember *temp_hostsmember = NULL;

	/* enable notification for top level host */
	if (affect_top_host == TRUE && level == 0)
		enable_host_notifications(hst);

	/* check all child hosts... */
	for (temp_hostsmember = hst->child_hosts; temp_hostsmember != NULL; temp_hostsmember = temp_hostsmember->next) {

		if ((child_host = temp_hostsmember->host_ptr) == NULL)
			continue;

		/* recurse... */
		enable_and_propagate_notifications(child_host, level + 1, affect_top_host, affect_hosts, affect_services);

		/* enable notifications for this host */
		if (affect_hosts == TRUE)
			enable_host_notifications(child_host);

		/* enable notifications for all services on this host... */
		if (affect_services == TRUE) {
			for (temp_servicesmember = child_host->services; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
				if ((temp_service = temp_servicesmember->service_ptr) == NULL)
					continue;
				enable_service_notifications(temp_service);
			}
		}
	}

	return;
}


/* disables notifications for all hosts and services "beyond" a given host */
void disable_and_propagate_notifications(host *hst, int level, int affect_top_host, int affect_hosts, int affect_services)
{
	host *child_host = NULL;
	service *temp_service = NULL;
	servicesmember *temp_servicesmember = NULL;
	hostsmember *temp_hostsmember = NULL;

	if (hst == NULL)
		return;

	/* disable notifications for top host */
	if (affect_top_host == TRUE && level == 0)
		disable_host_notifications(hst);

	/* check all child hosts... */
	for (temp_hostsmember = hst->child_hosts; temp_hostsmember != NULL; temp_hostsmember = temp_hostsmember->next) {

		if ((child_host = temp_hostsmember->host_ptr) == NULL)
			continue;

		/* recurse... */
		disable_and_propagate_notifications(child_host, level + 1, affect_top_host, affect_hosts, affect_services);

		/* disable notifications for this host */
		if (affect_hosts == TRUE)
			disable_host_notifications(child_host);

		/* disable notifications for all services on this host... */
		if (affect_services == TRUE) {
			for (temp_servicesmember = child_host->services; temp_servicesmember != NULL; temp_servicesmember = temp_servicesmember->next) {
				if ((temp_service = temp_servicesmember->service_ptr) == NULL)
					continue;
				disable_service_notifications(temp_service);
			}
		}
	}

	return;
}


/* enables host notifications for a contact */
void enable_contact_host_notifications(contact *cntct)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (cntct->host_notifications_enabled == TRUE)
		return;

	pre_modify_contact_attribute(cntct, attr);

	/* set the attribute modified flag */
	cntct->modified_host_attributes |= attr;

	/* enable the host notifications... */
	cntct->host_notifications_enabled = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_contact_data(NEBTYPE_ADAPTIVECONTACT_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, cntct, CMD_NONE, MODATTR_NONE, cntct->modified_attributes, attr, cntct->modified_host_attributes, MODATTR_NONE, cntct->modified_service_attributes, NULL);
#endif

	/* update the status log to reflect the new contact state */
	update_contact_status(cntct, FALSE);

	return;
}


/* disables host notifications for a contact */
void disable_contact_host_notifications(contact *cntct)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (cntct->host_notifications_enabled == FALSE)
		return;

	pre_modify_contact_attribute(cntct, attr);

	/* set the attribute modified flag */
	cntct->modified_host_attributes |= attr;

	/* enable the host notifications... */
	cntct->host_notifications_enabled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_contact_data(NEBTYPE_ADAPTIVECONTACT_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, cntct, CMD_NONE, MODATTR_NONE, cntct->modified_attributes, attr, cntct->modified_host_attributes, MODATTR_NONE, cntct->modified_service_attributes, NULL);
#endif

	/* update the status log to reflect the new contact state */
	update_contact_status(cntct, FALSE);

	return;
}


/* enables service notifications for a contact */
void enable_contact_service_notifications(contact *cntct)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (cntct->service_notifications_enabled == TRUE)
		return;

	pre_modify_contact_attribute(cntct, attr);

	/* set the attribute modified flag */
	cntct->modified_service_attributes |= attr;

	/* enable the host notifications... */
	cntct->service_notifications_enabled = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_contact_data(NEBTYPE_ADAPTIVECONTACT_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, cntct, CMD_NONE, MODATTR_NONE, cntct->modified_attributes, MODATTR_NONE, cntct->modified_host_attributes, attr, cntct->modified_service_attributes, NULL);
#endif

	/* update the status log to reflect the new contact state */
	update_contact_status(cntct, FALSE);

	return;
}


/* disables service notifications for a contact */
void disable_contact_service_notifications(contact *cntct)
{
	unsigned long attr = MODATTR_NOTIFICATIONS_ENABLED;

	/* no change */
	if (cntct->service_notifications_enabled == FALSE)
		return;

	pre_modify_contact_attribute(cntct, attr);

	/* set the attribute modified flag */
	cntct->modified_service_attributes |= attr;

	/* enable the host notifications... */
	cntct->service_notifications_enabled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_contact_data(NEBTYPE_ADAPTIVECONTACT_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, cntct, CMD_NONE, MODATTR_NONE, cntct->modified_attributes, MODATTR_NONE, cntct->modified_host_attributes, attr, cntct->modified_service_attributes, NULL);
#endif

	/* update the status log to reflect the new contact state */
	update_contact_status(cntct, FALSE);

	return;
}


/* schedules downtime for all hosts "beyond" a given host */
void schedule_and_propagate_downtime(host *temp_host, time_t entry_time, char *author, char *comment_data, time_t start_time, time_t end_time, int fixed, unsigned long triggered_by, unsigned long duration)
{
	host *child_host = NULL;
	hostsmember *temp_hostsmember = NULL;

	/* check all child hosts... */
	for (temp_hostsmember = temp_host->child_hosts; temp_hostsmember != NULL; temp_hostsmember = temp_hostsmember->next) {

		if ((child_host = temp_hostsmember->host_ptr) == NULL)
			continue;

		/* recurse... */
		schedule_and_propagate_downtime(child_host, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration);

		/* schedule downtime for this host */
		schedule_downtime(HOST_DOWNTIME, child_host->name, NULL, entry_time, author, comment_data, start_time, end_time, fixed, triggered_by, duration, NULL);
	}

	return;
}


/* acknowledges a host problem */
void acknowledge_host_problem(host *hst, char *ack_author, char *ack_data, int type, int notify, int persistent)
{
	time_t current_time = 0L;

	/* cannot acknowledge a non-existent problem */
	if (hst->current_state == HOST_UP)
		return;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_acknowledgement_data(NEBTYPE_ACKNOWLEDGEMENT_ADD, NEBFLAG_NONE, NEBATTR_NONE, HOST_ACKNOWLEDGEMENT, (void *)hst, ack_author, ack_data, type, notify, persistent, NULL);
#endif

	/* send out an acknowledgement notification */
	if (notify == TRUE)
		host_notification(hst, NOTIFICATION_ACKNOWLEDGEMENT, ack_author, ack_data, NOTIFICATION_OPTION_NONE);

	/* set the acknowledgement flag */
	hst->problem_has_been_acknowledged = TRUE;

	/* set the acknowledgement type */
	hst->acknowledgement_type = type ? ACKNOWLEDGEMENT_STICKY : ACKNOWLEDGEMENT_NORMAL;

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	/* add a comment for the acknowledgement */
	time(&current_time);
	add_new_host_comment(ACKNOWLEDGEMENT_COMMENT, hst->name, current_time, ack_author, ack_data, persistent, COMMENTSOURCE_INTERNAL, FALSE, (time_t)0, NULL);

	return;
}


/* acknowledges a service problem */
void acknowledge_service_problem(service *svc, char *ack_author, char *ack_data, int type, int notify, int persistent)
{
	time_t current_time = 0L;

	/* cannot acknowledge a non-existent problem */
	if (svc->current_state == STATE_OK)
		return;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_acknowledgement_data(NEBTYPE_ACKNOWLEDGEMENT_ADD, NEBFLAG_NONE, NEBATTR_NONE, SERVICE_ACKNOWLEDGEMENT, (void *)svc, ack_author, ack_data, type, notify, persistent, NULL);
#endif

	/* send out an acknowledgement notification */
	if (notify == TRUE)
		service_notification(svc, NOTIFICATION_ACKNOWLEDGEMENT, ack_author, ack_data, NOTIFICATION_OPTION_NONE);

	/* set the acknowledgement flag */
	svc->problem_has_been_acknowledged = TRUE;

	/* set the acknowledgement type */
	svc->acknowledgement_type = type ? ACKNOWLEDGEMENT_STICKY : ACKNOWLEDGEMENT_NORMAL;

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	/* add a comment for the acknowledgement */
	time(&current_time);
	add_new_service_comment(ACKNOWLEDGEMENT_COMMENT, svc->host_name, svc->description, current_time, ack_author, ack_data, persistent, COMMENTSOURCE_INTERNAL, FALSE, (time_t)0, NULL);

	return;
}


/* removes a host acknowledgement */
void remove_host_acknowledgement(host *hst)
{

	/* set the acknowledgement flag */
	hst->problem_has_been_acknowledged = FALSE;

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	/* remove any non-persistant comments associated with the ack */
	delete_host_acknowledgement_comments(hst);

	return;
}


/* removes a service acknowledgement */
void remove_service_acknowledgement(service *svc)
{

	/* set the acknowledgement flag */
	svc->problem_has_been_acknowledged = FALSE;

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	/* remove any non-persistant comments associated with the ack */
	delete_service_acknowledgement_comments(svc);

	return;
}


/* starts executing service checks */
void start_executing_service_checks(void)
{
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* bail out if we're already executing services */
	if (execute_service_checks == TRUE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the service check execution flag */
	execute_service_checks = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* stops executing service checks */
void stop_executing_service_checks(void)
{
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* bail out if we're already not executing services */
	if (execute_service_checks == FALSE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the service check execution flag */
	execute_service_checks = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* starts accepting passive service checks */
void start_accepting_passive_service_checks(void)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* bail out if we're already accepting passive services */
	if (accept_passive_service_checks == TRUE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the service check flag */
	accept_passive_service_checks = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* stops accepting passive service checks */
void stop_accepting_passive_service_checks(void)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* bail out if we're already not accepting passive services */
	if (accept_passive_service_checks == FALSE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the service check flag */
	accept_passive_service_checks = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* enables passive service checks for a particular service */
void enable_passive_service_checks(service *svc)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* no change */
	if (svc->accept_passive_checks == TRUE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* set the passive check flag */
	svc->accept_passive_checks = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	return;
}


/* disables passive service checks for a particular service */
void disable_passive_service_checks(service *svc)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* no change */
	if (svc->accept_passive_checks == FALSE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* set the passive check flag */
	svc->accept_passive_checks = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	return;
}


/* starts executing host checks */
void start_executing_host_checks(void)
{
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* bail out if we're already executing hosts */
	if (execute_host_checks == TRUE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the host check execution flag */
	execute_host_checks = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* stops executing host checks */
void stop_executing_host_checks(void)
{
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* bail out if we're already not executing hosts */
	if (execute_host_checks == FALSE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the host check execution flag */
	execute_host_checks = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* starts accepting passive host checks */
void start_accepting_passive_host_checks(void)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* bail out if we're already accepting passive hosts */
	if (accept_passive_host_checks == TRUE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the host check flag */
	accept_passive_host_checks = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* stops accepting passive host checks */
void stop_accepting_passive_host_checks(void)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* bail out if we're already not accepting passive hosts */
	if (accept_passive_host_checks == FALSE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the host check flag */
	accept_passive_host_checks = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* enables passive host checks for a particular host */
void enable_passive_host_checks(host *hst)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* no change */
	if (hst->accept_passive_checks == TRUE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the passive check flag */
	hst->accept_passive_checks = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* disables passive host checks for a particular host */
void disable_passive_host_checks(host *hst)
{
	unsigned long attr = MODATTR_PASSIVE_CHECKS_ENABLED;

	/* no change */
	if (hst->accept_passive_checks == FALSE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the passive check flag */
	hst->accept_passive_checks = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* enables event handlers on a program-wide basis */
void start_using_event_handlers(void)
{
	unsigned long attr = MODATTR_EVENT_HANDLER_ENABLED;

	/* no change */
	if (enable_event_handlers == TRUE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;
	modified_service_process_attributes |= attr;

	/* set the event handler flag */
	enable_event_handlers = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* disables event handlers on a program-wide basis */
void stop_using_event_handlers(void)
{
	unsigned long attr = MODATTR_EVENT_HANDLER_ENABLED;

	/* no change */
	if (enable_event_handlers == FALSE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;
	modified_service_process_attributes |= attr;

	/* set the event handler flag */
	enable_event_handlers = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* enables the event handler for a particular service */
void enable_service_event_handler(service *svc)
{
	unsigned long attr = MODATTR_EVENT_HANDLER_ENABLED;

	/* no change */
	if (svc->event_handler_enabled == TRUE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* set the event handler flag */
	svc->event_handler_enabled = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	return;
}


/* disables the event handler for a particular service */
void disable_service_event_handler(service *svc)
{
	unsigned long attr = MODATTR_EVENT_HANDLER_ENABLED;

	/* no change */
	if (svc->event_handler_enabled == FALSE)
		return;

	pre_modify_service_attribute(svc, attr);

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* set the event handler flag */
	svc->event_handler_enabled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	return;
}


/* enables the event handler for a particular host */
void enable_host_event_handler(host *hst)
{
	unsigned long attr = MODATTR_EVENT_HANDLER_ENABLED;

	/* no change */
	if (hst->event_handler_enabled == TRUE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the event handler flag */
	hst->event_handler_enabled = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* disables the event handler for a particular host */
void disable_host_event_handler(host *hst)
{
	unsigned long attr = MODATTR_EVENT_HANDLER_ENABLED;

	/* no change */
	if (hst->event_handler_enabled == FALSE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the event handler flag */
	hst->event_handler_enabled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* disables checks of a particular host */
void disable_host_checks(host *hst)
{
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* checks are already disabled */
	if (hst->checks_enabled == FALSE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the host check flag */
	hst->checks_enabled = FALSE;
	hst->should_be_scheduled = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* enables checks of a particular host */
void enable_host_checks(host *hst)
{
	time_t preferred_time = 0L;
	time_t next_valid_time = 0L;
	unsigned long attr = MODATTR_ACTIVE_CHECKS_ENABLED;

	/* checks are already enabled */
	if (hst->checks_enabled == TRUE)
		return;

	pre_modify_host_attribute(hst, attr);

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the host check flag */
	hst->checks_enabled = TRUE;
	hst->should_be_scheduled = TRUE;

	/* hosts with no check intervals don't get checked */
	if (hst->check_interval == 0)
		hst->should_be_scheduled = FALSE;

	/* schedule a check for right now (or as soon as possible) */
	time(&preferred_time);
	if (check_time_against_period(preferred_time, hst->check_period_ptr) == ERROR) {
		get_next_valid_time(preferred_time, &next_valid_time, hst->check_period_ptr);
		hst->next_check = next_valid_time;
	} else
		hst->next_check = preferred_time;

	/* schedule a check if we should */
	if (hst->should_be_scheduled == TRUE)
		schedule_host_check(hst, hst->next_check, CHECK_OPTION_NONE);

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* start obsessing over service check results */
void start_obsessing_over_service_checks(void)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (obsess_over_services == TRUE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the service obsession flag */
	obsess_over_services = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* stop obsessing over service check results */
void stop_obsessing_over_service_checks(void)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (obsess_over_services == FALSE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the service obsession flag */
	obsess_over_services = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* start obsessing over host check results */
void start_obsessing_over_host_checks(void)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (obsess_over_hosts == TRUE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the host obsession flag */
	obsess_over_hosts = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* stop obsessing over host check results */
void stop_obsessing_over_host_checks(void)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (obsess_over_hosts == FALSE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the host obsession flag */
	obsess_over_hosts = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* enables service freshness checking */
void enable_service_freshness_checks(void)
{
	unsigned long attr = MODATTR_FRESHNESS_CHECKS_ENABLED;

	/* no change */
	if (check_service_freshness == TRUE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the freshness check flag */
	check_service_freshness = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* disables service freshness checking */
void disable_service_freshness_checks(void)
{
	unsigned long attr = MODATTR_FRESHNESS_CHECKS_ENABLED;

	/* no change */
	if (check_service_freshness == FALSE)
		return;

	/* set the attribute modified flag */
	modified_service_process_attributes |= attr;

	/* set the freshness check flag */
	check_service_freshness = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, MODATTR_NONE, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* enables host freshness checking */
void enable_host_freshness_checks(void)
{
	unsigned long attr = MODATTR_FRESHNESS_CHECKS_ENABLED;

	/* no change */
	if (check_host_freshness == TRUE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the freshness check flag */
	check_host_freshness = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* disables host freshness checking */
void disable_host_freshness_checks(void)
{
	unsigned long attr = MODATTR_FRESHNESS_CHECKS_ENABLED;

	/* no change */
	if (check_host_freshness == FALSE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;

	/* set the freshness check flag */
	check_host_freshness = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, MODATTR_NONE, modified_service_process_attributes, NULL);
#endif

	/* update the status log with the program info */
	update_program_status(FALSE);

	return;
}


/* enable performance data on a program-wide basis */
void enable_performance_data(void)
{
	unsigned long attr = MODATTR_PERFORMANCE_DATA_ENABLED;

	/* bail out if we're already set... */
	if (process_performance_data == TRUE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;
	modified_service_process_attributes |= attr;

	process_performance_data = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log */
	update_program_status(FALSE);

	return;
}


/* disable performance data on a program-wide basis */
void disable_performance_data(void)
{
	unsigned long attr = MODATTR_PERFORMANCE_DATA_ENABLED;

#	/* bail out if we're already set... */
	if (process_performance_data == FALSE)
		return;

	/* set the attribute modified flag */
	modified_host_process_attributes |= attr;
	modified_service_process_attributes |= attr;

	process_performance_data = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_program_data(NEBTYPE_ADAPTIVEPROGRAM_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, CMD_NONE, attr, modified_host_process_attributes, attr, modified_service_process_attributes, NULL);
#endif

	/* update the status log */
	update_program_status(FALSE);

	return;
}


/* start obsessing over a particular service */
void start_obsessing_over_service(service *svc)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (svc->obsess == TRUE)
		return;

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* set the obsess over service flag */
	svc->obsess = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	return;
}


/* stop obsessing over a particular service */
void stop_obsessing_over_service(service *svc)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (svc->obsess == FALSE)
		return;

	/* set the attribute modified flag */
	svc->modified_attributes |= attr;

	/* set the obsess over service flag */
	svc->obsess = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_service_data(NEBTYPE_ADAPTIVESERVICE_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, svc, CMD_NONE, attr, svc->modified_attributes, NULL);
#endif

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	return;
}


/* start obsessing over a particular host */
void start_obsessing_over_host(host *hst)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (hst->obsess == TRUE)
		return;

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the obsess flag */
	hst->obsess = TRUE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* stop obsessing over a particular host */
void stop_obsessing_over_host(host *hst)
{
	unsigned long attr = MODATTR_OBSESSIVE_HANDLER_ENABLED;

	/* no change */
	if (hst->obsess == FALSE)
		return;

	/* set the attribute modified flag */
	hst->modified_attributes |= attr;

	/* set the obsess over host flag */
	hst->obsess = FALSE;

#ifdef USE_EVENT_BROKER
	/* send data to event broker */
	broker_adaptive_host_data(NEBTYPE_ADAPTIVEHOST_UPDATE, NEBFLAG_NONE, NEBATTR_NONE, hst, CMD_NONE, attr, hst->modified_attributes, NULL);
#endif

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* sets the current notification number for a specific host */
void set_host_notification_number(host *hst, int num)
{

	/* set the notification number */
	hst->current_notification_number = num;

	/* update the status log with the host info */
	update_host_status(hst, FALSE);

	return;
}


/* sets the current notification number for a specific service */
void set_service_notification_number(service *svc, int num)
{

	/* set the notification number */
	svc->current_notification_number = num;

	/* update the status log with the service info */
	update_service_status(svc, FALSE);

	return;
}
