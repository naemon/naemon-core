#include "config.h"
#include "common.h"
#include "nebmods.h"
#include "neberrors.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"
#include <string.h>

static nebmodule *neb_module_list;
static nebcallback **neb_callback_list;

/* compat stuff for USE_LTDL */
#ifndef HAVE_DLFCN_H
# define dlopen(p, flags) lt_dlopen(p)
# define dlclose(p) lt_dlclose(p)
# define dlerror() lt_dlerror()
# define dlsym(hdl, sym) lt_dlsym(hdl, sym)
# define RTLD_NOW 0
# define RTLD_GLOBAL 0
#else
# include <dlfcn.h>
# define lt_dlinit() 0
# define lt_dlexit() 0
#endif

struct neb_cb_result {
	int rc;
	char *description;
	char *module_name;
};

struct neb_cb_resultset {
	GPtrArray *cb_results;
};

typedef struct neb_cb_resultset_iter_real {
	neb_cb_resultset *iter_target;
	ssize_t idx;
} neb_cb_resultset_iter_real;

void neb_cb_result_g_ptr_array_clear(gpointer data);
/****************************************************************************/
/****************************************************************************/
/* INITIALIZATION/CLEANUP FUNCTIONS                                         */
/****************************************************************************/
/****************************************************************************/

/* initialize module routines */
int neb_init_modules(void)
{
	if (lt_dlinit())
		return ERROR;
	return OK;
}


/* deinitialize module routines */
int neb_deinit_modules(void)
{
	if (lt_dlexit())
		return ERROR;
	return OK;
}


/* add a new module to module list */
int neb_add_module(char *filename, char *args, int should_be_loaded)
{
	nebmodule *new_module = NULL;
	int x = OK;

	if (filename == NULL)
		return ERROR;

	/* allocate memory */
	new_module = nm_calloc(1, sizeof(nebmodule));

	/* initialize vars */
	new_module->filename = nm_strdup(filename);
	new_module->args = (args == NULL) ? NULL : nm_strdup(args);
	new_module->should_be_loaded = should_be_loaded;
	new_module->is_currently_loaded = FALSE;
	for (x = 0; x < NEBMODULE_MODINFO_NUMITEMS; x++)
		new_module->info[x] = NULL;
	new_module->module_handle = NULL;
	new_module->init_func = NULL;
	new_module->deinit_func = NULL;

	/* add module to head of list */
	new_module->next = neb_module_list;
	neb_module_list = new_module;

	log_debug_info(DEBUGL_EVENTBROKER, 0, "Added module: name='%s', args='%s', should_be_loaded='%d'\n", filename, args, should_be_loaded);

	return OK;
}


int neb_add_core_module(nebmodule *mod)
{
	mod->should_be_loaded = FALSE;
	mod->is_currently_loaded = TRUE;
	mod->core_module = TRUE;
	mod->module_handle = mod;
	mod->next = neb_module_list;
	neb_module_list = mod;
	return 0;
}

/* free memory allocated to module list */
int neb_free_module_list(void)
{
	nebmodule *temp_module = NULL;
	nebmodule *next_module = NULL;
	int x = OK;

	for (temp_module = neb_module_list; temp_module; temp_module = next_module) {
		next_module = temp_module->next;

		for (x = 0; x < NEBMODULE_MODINFO_NUMITEMS; x++)
			nm_free(temp_module->info[x]);

		/* don't free this stuff for core modules */
		if (temp_module->core_module)
			continue;
		nm_free(temp_module->filename);
		nm_free(temp_module->args);
		nm_free(temp_module);
	}

	neb_module_list = NULL;

	return OK;
}



/****************************************************************************/
/****************************************************************************/
/* LOAD/UNLOAD FUNCTIONS                                                    */
/****************************************************************************/
/****************************************************************************/


/* load all modules */
int neb_load_all_modules(void)
{
	nebmodule *temp_module = NULL;
	int ret, errors = 0;

	for (temp_module = neb_module_list; temp_module; temp_module = temp_module->next) {
		ret = neb_load_module(temp_module);
		if (ret != OK) {
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to load module '%s'.\n", temp_module->filename ? temp_module->filename : "(no file?)");
			errors++;
		}
	}

	return errors ? ERROR : OK;
}


/* load a particular module */
int neb_load_module(nebmodule *mod)
{
	int (*initfunc)(int, char *, void *);
	int *module_version_ptr = NULL;
	int result = OK;

	if (mod == NULL)
		return ERROR;

	/* don't reopen the module */
	if (mod->is_currently_loaded == TRUE)
		return OK;

	/* don't load modules unless they should be loaded */
	if (mod->should_be_loaded == FALSE || mod->filename == NULL)
		return ERROR;

	/* load the module */
	mod->module_handle = dlopen(mod->filename, RTLD_NOW);
	if (mod->module_handle == NULL) {
		nm_log(NSLOG_RUNTIME_ERROR, "Error: Could not load module '%s' -> %s\n", mod->filename, dlerror());

		return ERROR;
	}

	/* mark the module as being loaded */
	mod->is_currently_loaded = TRUE;

	/* find module API version */
	module_version_ptr = (int *)dlsym(mod->module_handle, "__neb_api_version");

	/* check the module API version */
	if (module_version_ptr == NULL || ((*module_version_ptr) != CURRENT_NEB_API_VERSION)) {
		if (module_version_ptr == NULL) {
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Module '%s' did not specify a version of the event broker API. Module will be unloaded.\n", mod->filename);
		} else {
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Module '%s' is using an incompatible version (v%d) of the event broker API (current version: v%d). Module will be unloaded.\n", mod->filename, *module_version_ptr, CURRENT_NEB_API_VERSION);
		}
		neb_unload_module(mod, NEBMODULE_FORCE_UNLOAD, NEBMODULE_ERROR_API_VERSION);

		return ERROR;
	}

	/* locate the initialization function */
	mod->init_func = dlsym(mod->module_handle, "nebmodule_init");

	/* if the init function could not be located, unload the module */
	if (mod->init_func == NULL) {

		nm_log(NSLOG_RUNTIME_ERROR, "Error: Could not locate nebmodule_init() in module '%s'.  Module will be unloaded.\n", mod->filename);

		neb_unload_module(mod, NEBMODULE_FORCE_UNLOAD, NEBMODULE_ERROR_NO_INIT);

		return ERROR;
	}

	/* run the module's init function */
	initfunc = mod->init_func;
	result = (*initfunc)(NEBMODULE_NORMAL_LOAD, mod->args, mod->module_handle);

	/* if the init function returned an error, unload the module */
	if (result != OK) {

		nm_log(NSLOG_RUNTIME_ERROR, "Error: Function nebmodule_init() in module '%s' returned an error.  Module will be unloaded.\n", mod->filename);

		neb_unload_module(mod, NEBMODULE_FORCE_UNLOAD, NEBMODULE_ERROR_BAD_INIT);

		return ERROR;
	}

	nm_log(NSLOG_INFO_MESSAGE, "Event broker module '%s' initialized successfully.\n", mod->filename);

	/* locate the de-initialization function (may or may not be present) */
	mod->deinit_func = dlsym(mod->module_handle, "nebmodule_deinit");

	log_debug_info(DEBUGL_EVENTBROKER, 0, "Module '%s' loaded with return code of '%d'\n", mod->filename, result);
	if (mod->deinit_func != NULL)
		log_debug_info(DEBUGL_EVENTBROKER, 0, "nebmodule_deinit() found\n");

	return OK;
}


/* close (unload) all modules that are currently loaded */
int neb_unload_all_modules(int flags, int reason)
{
	nebmodule *temp_module;

	for (temp_module = neb_module_list; temp_module; temp_module = temp_module->next) {

		/* skip modules that are not loaded */
		if (temp_module->is_currently_loaded == FALSE)
			continue;

		/* skip modules that do not have a valid handle */
		if (temp_module->module_handle == NULL)
			continue;

		/* close/unload the module */
		neb_unload_module(temp_module, flags, reason);
	}

	return OK;
}



/* close (unload) a particular module */
int neb_unload_module(nebmodule *mod, int flags, int reason)
{
	int (*deinitfunc)(int, int);
	int result = OK;

	if (mod == NULL)
		return ERROR;

	log_debug_info(DEBUGL_EVENTBROKER, 0, "Attempting to unload module '%s': flags=%d, reason=%d\n", mod->filename, flags, reason);

	/* call the de-initialization function if available (and the module was initialized) */
	if (mod->deinit_func && reason != NEBMODULE_ERROR_BAD_INIT) {

		deinitfunc = mod->deinit_func;

		/* module can opt to not be unloaded */
		result = (*deinitfunc)(flags, reason);

		/* if module doesn't want to be unloaded, exit with error (unless its being forced) */
		if (result != OK && !(flags & NEBMODULE_FORCE_UNLOAD))
			return ERROR;
	}

	/* deregister all of the module's callbacks */
	neb_deregister_module_callbacks(mod);

	if (mod->core_module == FALSE) {

		/* unload the module */
		result = dlclose(mod->module_handle);

		if (result != 0) {
			nm_log(NSLOG_RUNTIME_ERROR, "Error: Could not unload module '%s' -> %s\n", mod->filename, dlerror());
			return ERROR;
		}
	}

	/* mark the module as being unloaded */
	mod->is_currently_loaded = FALSE;

	log_debug_info(DEBUGL_EVENTBROKER, 0, "Module '%s' unloaded successfully.\n", mod->filename);

	nm_log(NSLOG_INFO_MESSAGE, "Event broker module '%s' deinitialized successfully.\n", mod->filename);

	return OK;
}




/****************************************************************************/
/****************************************************************************/
/* INFO FUNCTIONS                                                           */
/****************************************************************************/
/****************************************************************************/

/* sets module information */
int neb_set_module_info(void *handle, int type, char *data)
{
	nebmodule *temp_module = NULL;

	if (handle == NULL)
		return NEBERROR_NOMODULE;

	/* check type */
	if (type < 0 || type >= NEBMODULE_MODINFO_NUMITEMS)
		return NEBERROR_MODINFOBOUNDS;

	/* find the module */
	for (temp_module = neb_module_list; temp_module != NULL; temp_module = temp_module->next) {
		if (temp_module->module_handle == handle)
			break;
	}
	if (temp_module == NULL)
		return NEBERROR_BADMODULEHANDLE;

	/* free any previously allocated memory */
	nm_free(temp_module->info[type]);

	/* allocate memory for the new data */
	temp_module->info[type] = nm_strdup(data);
	return OK;
}



/****************************************************************************/
/****************************************************************************/
/* CALLBACK FUNCTIONS                                                       */
/****************************************************************************/
/****************************************************************************/

static neb_cb_resultset *neb_cb_resultset_create(void)
{
	neb_cb_resultset *cbrs = nm_malloc(sizeof(*cbrs));
	cbrs->cb_results = g_ptr_array_new_with_free_func(neb_cb_result_g_ptr_array_clear);
	return cbrs;
}

void neb_cb_resultset_destroy(neb_cb_resultset *cbrs)
{
	if (cbrs == NULL)
		return;
	g_ptr_array_free(cbrs->cb_results, TRUE);
	nm_free(cbrs);
}

void neb_cb_resultset_iter_init(neb_cb_resultset_iter *iter, neb_cb_resultset *rs)
{
	neb_cb_resultset_iter_real *it = (neb_cb_resultset_iter_real *) iter;
	it->iter_target = rs;
	it->idx = -1;
}

neb_cb_resultset_iter *neb_cb_resultset_iter_next(neb_cb_resultset_iter *iter, neb_cb_result **result)
{
	neb_cb_resultset_iter_real *it = (neb_cb_resultset_iter_real *) iter;

	if (!it || !it->iter_target)
		return NULL;

	++it->idx;
	g_warn_if_fail(it->idx >= 0);
	if ((gsize) it->idx >= it->iter_target->cb_results->len) {
		it->idx = -1;
		*result = NULL;
		return NULL;
	}

	*result = g_ptr_array_index(it->iter_target->cb_results, it->idx);
	return (neb_cb_resultset_iter *)it;
}

neb_cb_result *neb_cb_result_create_full(int rc, const char *format, ...)
{
	va_list ap;
	neb_cb_result *res = nm_malloc(sizeof(*res));
	res->module_name = NULL;
	res->rc = rc;

	va_start(ap, format);
	if (vasprintf(&(res->description), format, ap) < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Failed to set description for neb_cb_result.");
		res->description = nm_strdup("<failed to set description>");
	}
	va_end(ap);
	return res;
}

neb_cb_result *neb_cb_result_create(int rc)
{
	return neb_cb_result_create_full(rc, "");
}

const char *neb_cb_result_module_name(neb_cb_result *cb_result)
{
	return cb_result->module_name;
}

const char *neb_cb_result_description(neb_cb_result *cb_result)
{
	return cb_result->description;
}

void neb_cb_result_set_description(neb_cb_result *cb_result, const char *format, ...)
{
	va_list ap;
	nm_free(cb_result->description);

	va_start(ap, format);
	if (vasprintf(&(cb_result->description), format, ap) < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "Failed to set description for neb_cb_result.");
		cb_result->description = nm_strdup("<failed to set description>");
	}
	va_end(ap);
}

int neb_cb_result_returncode(neb_cb_result *cb_result)
{
	return cb_result->rc;
}

void neb_cb_result_destroy(neb_cb_result *res)
{
	nm_free(res->module_name);
	nm_free(res->description);
	nm_free(res);
}

void neb_cb_result_g_ptr_array_clear(gpointer data)
{
	neb_cb_result_destroy(data);
}

/* allows a module to register a callback function */
int neb_register_callback_full(enum NEBCallbackType callback_type, void *mod_handle, int priority, enum NEBCallbackAPIVersion api_version, void *callback_func)
{
	nebmodule *temp_module = NULL;
	nebcallback *new_callback = NULL;
	nebcallback *temp_callback = NULL;
	nebcallback *last_callback = NULL;

	if (callback_func == NULL)
		return NEBERROR_NOCALLBACKFUNC;

	if (neb_callback_list == NULL)
		return NEBERROR_NOCALLBACKLIST;

	if (mod_handle == NULL)
		return NEBERROR_NOMODULEHANDLE;

	/* make sure module handle is valid */
	for (temp_module = neb_module_list; temp_module; temp_module = temp_module->next) {
		if (temp_module->module_handle == mod_handle)
			break;
	}
	if (temp_module == NULL)
		return NEBERROR_BADMODULEHANDLE;

	/* allocate memory */
	new_callback = nm_malloc(sizeof(nebcallback));
	new_callback->priority = priority;
	new_callback->module_handle = mod_handle;
	new_callback->callback_func = callback_func;
	new_callback->api_version = api_version;

	/* add new function to callback list, sorted by priority (first come, first served for same priority) */
	new_callback->next = NULL;
	if (neb_callback_list[callback_type] == NULL)
		neb_callback_list[callback_type] = new_callback;
	else {
		last_callback = NULL;
		for (temp_callback = neb_callback_list[callback_type]; temp_callback != NULL; temp_callback = temp_callback->next) {
			if (temp_callback->priority > new_callback->priority)
				break;
			last_callback = temp_callback;
		}
		if (last_callback == NULL)
			neb_callback_list[callback_type] = new_callback;
		else {
			if (temp_callback == NULL)
				last_callback->next = new_callback;
			else {
				new_callback->next = temp_callback;
				last_callback->next = new_callback;
			}
		}
	}

	return OK;
}

int neb_register_callback(enum NEBCallbackType callback_type, void *mod_handle, int priority, int (*callback_func)(int, void *))
{
	return neb_register_callback_full(callback_type, mod_handle, priority, NEB_API_VERSION_1, callback_func);
}


/* dregisters all callback functions for a given module */
int neb_deregister_module_callbacks(nebmodule *mod)
{
	nebcallback *temp_callback = NULL;
	nebcallback *next_callback = NULL;
	enum NEBCallbackType callback_type;

	if (mod == NULL)
		return NEBERROR_NOMODULE;

	if (neb_callback_list == NULL)
		return OK;

	for (callback_type = 0; callback_type < NEBCALLBACK_NUMITEMS; callback_type++) {
		for (temp_callback = neb_callback_list[callback_type]; temp_callback != NULL; temp_callback = next_callback) {
			next_callback = temp_callback->next;
			if (temp_callback->module_handle == mod->module_handle)
				neb_deregister_callback(callback_type, (int(*)(int, void *))temp_callback->callback_func);
		}

	}

	return OK;
}


/* allows a module to deregister a callback function */
int neb_deregister_callback(enum NEBCallbackType callback_type, void *callback_func)
{
	nebcallback *temp_callback = NULL;
	nebcallback *last_callback = NULL;
	nebcallback *next_callback = NULL;

	if (callback_func == NULL)
		return NEBERROR_NOCALLBACKFUNC;

	if (neb_callback_list == NULL)
		return NEBERROR_NOCALLBACKLIST;

	/* find the callback to remove */
	for (temp_callback = last_callback = neb_callback_list[callback_type]; temp_callback != NULL; temp_callback = next_callback) {
		next_callback = temp_callback->next;

		/* we found it */
		if (temp_callback->callback_func == callback_func)
			break;

		last_callback = temp_callback;
	}

	/* we couldn't find the callback */
	if (temp_callback == NULL)
		return NEBERROR_CALLBACKNOTFOUND;

	else {
		/* only one item in the list */
		if (temp_callback != last_callback->next)
			neb_callback_list[callback_type] = NULL;
		else
			last_callback->next = next_callback;
		nm_free(temp_callback);
	}

	return OK;
}

static neb_cb_result *neb_invoke_callback(void *cb, enum NEBCallbackAPIVersion api_version, enum NEBCallbackType callback_type, void *user_data)
{
	neb_cb_result *cbresult = NULL;
	switch (api_version) {
	case NEB_API_VERSION_2: {
		neb_cb_result *(*callbackfunc)(int, void *) = cb;
		cbresult = callbackfunc(callback_type, user_data);
	}
	break;
	case NEB_API_VERSION_1: {
		int (*callbackfunc)(int, void *) = cb;
		int rc = callbackfunc(callback_type, user_data);
		cbresult = neb_cb_result_create_full(rc, "No description available, callback invoked using API version 1");
	}
	break;
	}

	return cbresult;
}

/* make callbacks to modules */
neb_cb_resultset *neb_make_callbacks_full(enum NEBCallbackType callback_type, void *data)
{
	nebcallback *temp_callback, *next_callback;
	nebmodule *temp_module;
	neb_cb_resultset *resultset = neb_cb_resultset_create();
	neb_cb_result *cbresult = NULL;
	int total_callbacks = 0;
	char *temp_module_name = "";

	/* make sure callback list is initialized */
	if (neb_callback_list == NULL) {
		cbresult = neb_cb_result_create_full(ERROR, "Uninitialized callback list");
		g_ptr_array_add(resultset->cb_results, cbresult);
		return resultset;
	}

	log_debug_info(DEBUGL_EVENTBROKER, 1, "Making callbacks (type %d)...\n", callback_type);

	/* make the callbacks... */
	for (temp_callback = neb_callback_list[callback_type]; temp_callback; temp_callback = next_callback) {
		next_callback = temp_callback->next;
		/* get name of module responsible for the callback */
		for (temp_module = neb_module_list; temp_module != NULL; temp_module = temp_module->next) {
			if (temp_module->module_handle == temp_callback->module_handle) {
				if (temp_module->core_module) {
					temp_module_name = "Unnamed core module";
				} else {
					temp_module_name = temp_module->filename;
				}
				break;
			}
		}
		cbresult = neb_invoke_callback(temp_callback->callback_func, temp_callback->api_version, callback_type, data);
		cbresult->module_name = nm_strdup(temp_module_name);
		g_ptr_array_add(resultset->cb_results, cbresult);
		temp_callback = next_callback;

		total_callbacks++;
		log_debug_info(DEBUGL_EVENTBROKER, 2, "Callback #%d (type %d) return code = %d\n", total_callbacks, callback_type, cbresult->rc);

		/* module wants to cancel callbacks to other modules (and potentially cancel the default Nagios handling of an event) */
		if (cbresult->rc == NEBERROR_CALLBACKCANCEL)
			break;

		/* module wants to override default Nagios handling of an event */
		/* not sure if we should bail out here just because one module wants to override things - what about other modules? EG 12/11/2006 */
		else if (cbresult->rc == NEBERROR_CALLBACKOVERRIDE)
			break;
	}

	return resultset;
}

int neb_make_callbacks(enum NEBCallbackType callback_type, void *data)
{
	neb_cb_resultset_iter iter;
	int rc = 0;
	neb_cb_result *cb_result = NULL;
	neb_cb_resultset *resultset = neb_make_callbacks_full(callback_type, data);

	neb_cb_resultset_iter_init(&iter, resultset);
	while (neb_cb_resultset_iter_next(&iter, &cb_result)) {
		rc = cb_result->rc;
	}

	neb_cb_resultset_destroy(resultset);
	return rc;
}


/* initialize callback list */
int neb_init_callback_list(void)
{
	register int x = 0;

	/* allocate memory for the callback list */
	neb_callback_list = nm_calloc(NEBCALLBACK_NUMITEMS, sizeof(nebcallback *));

	/* initialize list pointers */
	for (x = 0; x < NEBCALLBACK_NUMITEMS; x++)
		neb_callback_list[x] = NULL;

	return OK;
}


/* free memory allocated to callback list */
int neb_free_callback_list(void)
{
	nebcallback *temp_callback = NULL;
	nebcallback *next_callback = NULL;
	register int x = 0;

	if (neb_callback_list == NULL)
		return OK;

	for (x = 0; x < NEBCALLBACK_NUMITEMS; x++) {

		for (temp_callback = neb_callback_list[x]; temp_callback != NULL; temp_callback = next_callback) {
			next_callback = temp_callback->next;
			nm_free(temp_callback);
		}

		neb_callback_list[x] = NULL;
	}

	nm_free(neb_callback_list);

	return OK;
}
