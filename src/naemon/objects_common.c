#include "objects_common.h"
#include "logging.h"
#include "nm_alloc.h"
#include "xodtemplate.h"
#include <string.h>

customvariablesmember *add_custom_variable_to_object(customvariablesmember **object_ptr, char *varname, char *varvalue)
{
	customvariablesmember *new_customvariablesmember = NULL;

	/* make sure we have the data we need */
	if (object_ptr == NULL) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Custom variable object is NULL\n");
		return NULL;
	}

	if (varname == NULL || !strcmp(varname, "")) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Custom variable name is NULL\n");
		return NULL;
	}

	/* allocate memory for a new member */
	new_customvariablesmember = nm_malloc(sizeof(customvariablesmember));
	new_customvariablesmember->variable_name = nm_strdup(varname);
	if (varvalue)
		new_customvariablesmember->variable_value = nm_strdup(varvalue);
	else
		new_customvariablesmember->variable_value = NULL;

	/* set initial values */
	new_customvariablesmember->has_been_modified = FALSE;

	/* add the new member to the head of the member list */
	new_customvariablesmember->next = *object_ptr;
	*object_ptr = new_customvariablesmember;

	return new_customvariablesmember;
}

const char *opts2str(int opts, const struct flag_map *map, char ok_char)
{
	int i, pos = 0;
	static char buf[16];

	if (!opts)
		return "n";

	if (opts == OPT_ALL)
		return "a";

	if (flag_isset(opts, OPT_OK)) {
		flag_unset(opts, OPT_OK);
		buf[pos++] = ok_char;
		buf[pos++] = opts ? ',' : 0;
	}

	for (i = 0; map[i].name; i++) {
		if (flag_isset(opts, map[i].opt)) {
			buf[pos++] = map[i].ch;
			flag_unset(opts, map[i].opt);
			if (!opts)
				break;
			buf[pos++] = ',';
		}
	}
	buf[pos++] = 0;
	return buf;
}

const char *state_type_name(int state_type)
{
	return state_type == HARD_STATE ? "HARD" : "SOFT";
}

const char *check_type_name(int check_type)
{
	return check_type == CHECK_TYPE_PASSIVE ? "PASSIVE" : "ACTIVE";
}

void fcache_customvars(FILE *fp, customvariablesmember *cvlist)
{
	if (cvlist) {
		customvariablesmember *l;
		for (l = cvlist; l; l = l->next)
			fprintf(fp, "\t_%s\t%s\n", l->variable_name, (l->variable_value == NULL) ? XODTEMPLATE_NULL : l->variable_value);
	}
}
