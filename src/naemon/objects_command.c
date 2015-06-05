#include "objects_command.h"
#include "nm_alloc.h"
#include "logging.h"
#include <string.h>
#include <glib.h>

static GHashTable *command_hash_table = NULL;
command *command_list = NULL;
command **command_ary = NULL;

int init_objects_command(int elems)
{
	command_ary = nm_calloc(elems, sizeof(command*));
	command_hash_table = g_hash_table_new(g_str_hash, g_str_equal);
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
	if (command_hash_table)
		g_hash_table_destroy(command_hash_table);

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

	if (contains_illegal_object_chars(name) == TRUE) {
		nm_log(NSLOG_VERIFICATION_ERROR, "Error: The name of command '%s' contains one or more illegal characters.", name);
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
	if ((find_command(new_command->name))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Command '%s' has already been defined\n", new_command->name);
		return ERROR;
	}
	g_hash_table_insert(command_hash_table, new_command->name, new_command);

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
	if (!this_command)
		return;
	nm_free(this_command->name);
	nm_free(this_command->command_line);
	nm_free(this_command);
}

/* find a command with arguments still attached */
command *find_bang_command(const char *name)
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
	return name ? g_hash_table_lookup(command_hash_table, name) : NULL;
}

void fcache_command(FILE *fp, const command *temp_command)
{
	fprintf(fp, "define command {\n\tcommand_name\t%s\n\tcommand_line\t%s\n\t}\n\n",
	        temp_command->name, temp_command->command_line);
}
