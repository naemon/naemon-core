#include "objects_hostdependency.h"
#include "objects_timeperiod.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"

hostdependency *add_host_dependency(char *dependent_host_name, char *host_name, int dependency_type, int inherits_parent, int failure_options, char *dependency_period)
{
	hostdependency *new_hostdependency = NULL;
	host *parent, *child;
	timeperiod *tp = NULL;
	int result;
	size_t hdep_size = sizeof(*new_hostdependency);

	/* make sure we have what we need */
	parent = find_host(host_name);
	if (!parent) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Master host '%s' in hostdependency from '%s' to '%s' is not defined anywhere!\n",
		       host_name, dependent_host_name, host_name);
		return NULL;
	}
	child = find_host(dependent_host_name);
	if (!child) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Dependent host '%s' in hostdependency from '%s' to '%s' is not defined anywhere!\n",
		       dependent_host_name, dependent_host_name, host_name);
		return NULL;
	}
	if (dependency_period && !(tp = find_timeperiod(dependency_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Unable to locate dependency_period '%s' for %s->%s host dependency\n",
		       dependency_period, parent->name, child->name);
		return NULL ;
	}

	new_hostdependency = nm_calloc(1, sizeof(*new_hostdependency));
	new_hostdependency->dependent_host_ptr = child;
	new_hostdependency->master_host_ptr = parent;
	new_hostdependency->dependency_period_ptr = tp;

	/* assign vars. Objects are immutable, so no need to copy */
	new_hostdependency->dependent_host_name = child->name;
	new_hostdependency->host_name = parent->name;
	if (tp)
		new_hostdependency->dependency_period = tp->name;

	new_hostdependency->dependency_type = (dependency_type == EXECUTION_DEPENDENCY) ? EXECUTION_DEPENDENCY : NOTIFICATION_DEPENDENCY;
	new_hostdependency->inherits_parent = (inherits_parent > 0) ? TRUE : FALSE;
	new_hostdependency->failure_options = failure_options;

	if (dependency_type == NOTIFICATION_DEPENDENCY)
		result = prepend_unique_object_to_objectlist_ptr(&child->notify_deps, new_hostdependency, *compare_objects, &hdep_size);
	else
		result = prepend_unique_object_to_objectlist_ptr(&child->exec_deps, new_hostdependency, *compare_objects, &hdep_size);

	if (result != OK) {
		free(new_hostdependency);
		/* hack to avoid caller bombing out */
		return result == OBJECTLIST_DUPE ? (void *)1 : NULL;
	}

	new_hostdependency->id = num_objects.hostdependencies++;
	return new_hostdependency;
}

void destroy_hostdependency(hostdependency *this_hostdependency)
{
	if (!this_hostdependency)
		return;
	nm_free(this_hostdependency);
	num_objects.hostdependencies--;
}

void fcache_hostdependency(FILE *fp, const hostdependency *temp_hostdependency)
{
	fprintf(fp, "define hostdependency {\n");
	fprintf(fp, "\thost_name\t%s\n", temp_hostdependency->host_name);
	fprintf(fp, "\tdependent_host_name\t%s\n", temp_hostdependency->dependent_host_name);
	if (temp_hostdependency->dependency_period)
		fprintf(fp, "\tdependency_period\t%s\n", temp_hostdependency->dependency_period);
	fprintf(fp, "\tinherits_parent\t%d\n", temp_hostdependency->inherits_parent);
	fprintf(fp, "\t%s_failure_options\t%s\n",
	        temp_hostdependency->dependency_type == NOTIFICATION_DEPENDENCY ? "notification" : "execution",
	        opts2str(temp_hostdependency->failure_options, host_flag_map, 'o'));
	fprintf(fp, "\t}\n\n");
}
