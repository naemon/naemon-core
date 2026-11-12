#include "objects_servicedependency.h"
#include "objects_timeperiod.h"
#include "objectlist.h"
#include "nm_alloc.h"
#include "logging.h"
#include "objects_fcache.h"

servicedependency *add_service_dependency(char *dependent_host_name, char *dependent_service_description, char *host_name, char *service_description, int dependency_type, int inherits_parent, int failure_options, char *dependency_period)
{
	servicedependency *new_servicedependency = NULL;
	service *parent, *child;
	timeperiod *tp = NULL;
	int result;
	size_t sdep_size = sizeof(*new_servicedependency);

	/* make sure we have what we need */
	parent = find_service(host_name, service_description);
	if (!parent) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Master service '%s' on host '%s' is not defined anywhere!\n",
		       service_description, host_name);
		return NULL;
	}
	child = find_service(dependent_host_name, dependent_service_description);
	if (!child) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Dependent service '%s' on host '%s' is not defined anywhere!\n",
		       dependent_service_description, dependent_host_name);
		return NULL;
	}
	if (dependency_period && !(tp = find_timeperiod(dependency_period))) {
		nm_log(NSLOG_CONFIG_ERROR, "Error: Failed to locate timeperiod '%s' for dependency from service '%s' on host '%s' to service '%s' on host '%s'\n",
		       dependency_period, dependent_service_description, dependent_host_name, service_description, host_name);
		return NULL;
	}

	/* allocate memory for a new service dependency entry */
	new_servicedependency = nm_calloc(1, sizeof(*new_servicedependency));

	new_servicedependency->dependent_service_ptr = child;
	new_servicedependency->master_service_ptr = parent;
	new_servicedependency->dependency_period_ptr = tp;

	/* assign vars. object names are immutable, so no need to copy */
	new_servicedependency->dependent_host_name = child->host_name;
	new_servicedependency->dependent_service_description = child->description;
	new_servicedependency->host_name = parent->host_name;
	new_servicedependency->service_description = parent->description;
	if (tp)
		new_servicedependency->dependency_period = tp->name;

	new_servicedependency->dependency_type = (dependency_type == EXECUTION_DEPENDENCY) ? EXECUTION_DEPENDENCY : NOTIFICATION_DEPENDENCY;
	new_servicedependency->inherits_parent = (inherits_parent > 0) ? TRUE : FALSE;
	new_servicedependency->failure_options = failure_options;

	/*
	 * add new service dependency to its respective services.
	 * Ordering doesn't matter here as we'll have to check them
	 * all anyway. We avoid adding dupes though, since we can
	 * apparently get zillion's and zillion's of them.
	 */
	if (dependency_type == NOTIFICATION_DEPENDENCY)
		result = prepend_unique_object_to_objectlist_ptr(&child->notify_deps, new_servicedependency, &compare_objects, &sdep_size);
	else
		result = prepend_unique_object_to_objectlist_ptr(&child->exec_deps, new_servicedependency, &compare_objects, &sdep_size);

	if (result != OK) {
		free(new_servicedependency);
		/* hack to avoid caller bombing out */
		return result == OBJECTLIST_DUPE ? (void *)1 : NULL;
	}

	new_servicedependency->id = num_objects.servicedependencies++;
	return new_servicedependency;
}

void destroy_servicedependency(servicedependency *this_servicedependency)
{
	if (!this_servicedependency)
		return;
	nm_free(this_servicedependency);
	num_objects.servicedependencies--;
}

void nm_fcache_servicedependency(struct nm_writebuf *wb, const servicedependency *temp_servicedependency)
{
	nm_wb_lit(wb, "define servicedependency {\n");
	fc_str(wb, "host_name", temp_servicedependency->host_name);
	fc_str(wb, "service_description", temp_servicedependency->service_description);
	fc_str(wb, "dependent_host_name", temp_servicedependency->dependent_host_name);
	fc_str(wb, "dependent_service_description", temp_servicedependency->dependent_service_description);
	if (temp_servicedependency->dependency_period)
		fc_str(wb, "dependency_period", temp_servicedependency->dependency_period);
	fc_int(wb, "inherits_parent", temp_servicedependency->inherits_parent);
	if (temp_servicedependency->dependency_type == NOTIFICATION_DEPENDENCY)
		fc_str(wb, "notification_failure_options", opts2str(temp_servicedependency->failure_options, service_flag_map, 'o'));
	else
		fc_str(wb, "execution_failure_options", opts2str(temp_servicedependency->failure_options, service_flag_map, 'o'));
	nm_wb_lit(wb, "\t}\n\n");
}

void fcache_servicedependency(FILE *fp, const servicedependency *temp_servicedependency)
{
	struct nm_writebuf wb;

	fc_file_begin(&wb, fp);
	nm_fcache_servicedependency(&wb, temp_servicedependency);
	nm_writebuf_done(&wb);
}
