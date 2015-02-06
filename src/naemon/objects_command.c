#include "objects_command.h"
#include "nm_alloc.h"
#include "logging.h"
#include <string.h>

static dkhash_table *command_hash_table = NULL;
command *command_list = NULL;
command **command_ary = NULL;

int init_objects_command(int elems)
{
	if (!elems) {
		command_ary = NULL;
		command_hash_table = NULL;
		return ERROR;
	}
	command_ary = nm_calloc(elems, sizeof(command*));
	command_hash_table = dkhash_create(elems * 1.5);
	return OK;
}

void destroy_objects_command()
{
	unsigned int i;
	for (i = 0; i < num_objects.commands; i++) {
		command *this_command = command_ary[i];
		destroy_command(this_command);
	}
	command_list = NULL;
	dkhash_destroy(command_hash_table);
	command_hash_table = NULL;
	nm_free(command_ary);
	num_objects.commands = 0;
}

command *create_command(const char *name, const char *value)
{
	command *new_command = NULL;

	/* make sure we have the data we need */
	if ((name == NULL || !strcmp(name, "")) || (value == NULL || !strcmp(value, ""))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Command name of command line is NULL\n");
		return NULL;
	}

	/* allocate memory for the new command */
	new_command = nm_calloc(1, sizeof(*new_command));

	/* assign vars */
	new_command->name = nm_strdup(name);
	new_command->command_line = nm_strdup(value);

	return new_command;
}

int register_command(command *new_command)
{
	/* add new command to hash table */
	int result = dkhash_insert(command_hash_table, new_command->name, NULL, new_command);
	switch (result) {
	case DKHASH_EDUPE:
		nm_log(NSLOG_CONFIG_ERROR, "Error: Command '%s' has already been defined\n", new_command->name);
		return ERROR;
		break;
	case DKHASH_OK:
		break;
	default:
		nm_log(NSLOG_CONFIG_ERROR, "Error: Could not add command '%s' to hash table\n", new_command->name);
		return ERROR;
		break;
	}

	new_command->id = num_objects.commands++;
	command_ary[new_command->id] = new_command;
	if (new_command->id)
		command_ary[new_command->id - 1]->next = new_command;
	else
		command_list = new_command;

	return OK;
}

void destroy_command(command *this_command)
{
	nm_free(this_command->name);
	nm_free(this_command->command_line);
	nm_free(this_command);
}

/* find a command with arguments still attached */
command *find_bang_command(char *name)
{
	char *bang;
	command *cmd;

	if (!name)
		return NULL;

	bang = strchr(name, '!');
	if (!bang)
		return find_command(name);
	*bang = 0;
	cmd = find_command(name);
	*bang = '!';
	return cmd;
}

command *find_command(const char *name)
{
	return dkhash_get(command_hash_table, name, NULL);
}

void fcache_command(FILE *fp, command *temp_command)
{
	fprintf(fp, "define command {\n\tcommand_name\t%s\n\tcommand_line\t%s\n\t}\n\n",
	        temp_command->name, temp_command->command_line);
}
