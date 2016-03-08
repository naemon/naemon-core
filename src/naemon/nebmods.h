#ifndef _NEBMODS_H
#define _NEBMODS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "nebcallbacks.h"
#include "nebmodules.h"

NAGIOS_BEGIN_DECL

/***** MODULE STRUCTURES *****/

/* NEB module callback list struct */
typedef struct nebcallback_struct {
	void                        *callback_func;
	void                        *module_handle;
	int                         priority;
	enum NEBCallbackAPIVersion  api_version;
	struct nebcallback_struct   *next;
} nebcallback;


/***** MODULE FUNCTIONS *****/
int neb_init_modules(void);
int neb_deinit_modules(void);
int neb_load_all_modules(void);
int neb_load_module(nebmodule *);
int neb_free_module_list(void);
int neb_unload_all_modules(int, int);
int neb_unload_module(nebmodule *, int, int);
int neb_add_module(char *, char *, int);
int neb_add_core_module(nebmodule *mod);


/***** CALLBACK FUNCTIONS *****/
typedef struct neb_cb_result neb_cb_result;
typedef struct neb_cb_resultset neb_cb_resultset;

/* not for public consumption */
struct neb_cb_resultset_iter_ {
	void *private1;
	ssize_t private2;
};

typedef struct neb_cb_resultset_iter_ neb_cb_resultset_iter;
int neb_init_callback_list(void);
int neb_free_callback_list(void);
/**
 * Make callbacks to Event Broker Modules, and get the full result back
 * @param callback_type The callback type to invoke
 * @param user_data Opaque pointer passed to callback
 * @return A neb_cb_resultset containing neb_cb_result pointers, as created by the callbacks. To
 * free this result, use \p neb_cb_resultset_destroy(). To iterate over the
 * set, use \p neb_cb_resultset_iter.
 **/
neb_cb_resultset * neb_make_callbacks_full(enum NEBCallbackType callback_type, void * user_data);

/**
 * Make callbacks to Event Broker Modules (simplified)
 * This is identical to \p neb_make_callbacks_full, with the execption that it
 * automatically frees the result and only returns the return code of the last
 * callback invoked, in order to provide backwards compatibility. New code
 * should probably avoid using this function.
 * @param callback_type The callback type to invoke
 * @param user_data Opaque pointer passed to callback
 * @return The return code of the callback result
 */
int neb_make_callbacks(enum NEBCallbackType callback_type, void * user_data);

/***** CALLBACK RESULT *****/
/**
 * Create a new \p neb_cb_result with the given \p rc and a description formatted with
 * \p format. To free the returned result, use \p neb_cb_result_destroy().
 * @param rc The return code
 * @param printf()-style format string
 * @param ... arguments to format
 * @return a new \p neb_cb_result
 */
neb_cb_result *neb_cb_result_create_full(int rc, const char *format, ...);

/**
 * Create a new \p neb_cb_result with the given \p rc. The description of this result
 * is the empty string "", unless explicitly set with
 * neb_cb_result_set_description(). That is, calling this function is
 * equivalent to calling \p neb_cb_result_create_full(rc, ""). To free the
 * returned result, use \p neb_cb_result_destroy().
 * @param rc The return code
 * @return a new \p neb_cb_result
 */
neb_cb_result *neb_cb_result_create(int rc);

/**
 * Frees a \p neb_cb_result and associated resources
 * @param cb_result a \p neb_cb_result
 */
void neb_cb_result_destroy(neb_cb_result *);

/**
 * @param cb_result a \p neb_cb_result
 * @return The module name associated with this \p neb_cb_result
 */
const char * neb_cb_result_module_name(neb_cb_result *cb_result);

/**
 * @param cb_result a \p neb_cb_result
 * @return The description associated with this \p neb_cb_result
 */
const char * neb_cb_result_description(neb_cb_result *cb_result);

/**
 * Set the description for the given \p neb_cb_result formatted with \p format.
 * Any preexisting description will be freed and overwritten.
 * @param cb_result a \p neb_cb_result
 * @param printf()-style format string
 * @param ... arguments to format
 */
void neb_cb_result_set_description(neb_cb_result *cb_result, const char *format, ...);
/**
 * @param cb_result a \p neb_cb_result
 * @return The returncode associated with this \p neb_cb_result
 */
int neb_cb_result_returncode(neb_cb_result *cb_result);

/**
 * Frees a \p neb_cb_resultset and associated resources. Note
 * that this also frees all the contained \p neb_cb_results contained
 * withing the set.
 * @param cb_resultset a \p neb_cb_resultset
 */
void neb_cb_resultset_destroy(neb_cb_resultset *);

/**
 * Initializes an iterator and associates it with \p resultset. Modifying the result set
 * after calling this function invalidates the returned iterator.
 * @param iter an uninitialized \p neb_cb_resultset_iter
 * @param resultset a \p neb_cb_resultset
 */
void neb_cb_resultset_iter_init(neb_cb_resultset_iter *iter, neb_cb_resultset *resultset);

/**
 * Advances \p iter and retrieves the result that is now targeted. If NULL is
 * returned, \p result is not set and the iterator becomes invalid.
 * @param iter an initialized \p neb_cb_resultset_iter
 * @param result a location to store the \p neb_cb_result
 * @return \p NULL if the end of the \p neb_cb_resultset has been reached.
 */
neb_cb_resultset_iter *neb_cb_resultset_iter_next(neb_cb_resultset_iter *, neb_cb_result **);
NAGIOS_END_DECL
#endif
