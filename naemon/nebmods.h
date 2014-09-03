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
	void            *callback_func;
	void            *module_handle;
	int             priority;
	struct nebcallback_struct *next;
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
int neb_init_callback_list(void);
int neb_free_callback_list(void);
int neb_make_callbacks(int, void *);

NAGIOS_END_DECL
#endif
