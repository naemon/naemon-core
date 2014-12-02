#ifndef _NEBERRORS_H
#define _NEBERRORS_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif


/***** GENERIC DEFINES *****/
#define NEB_OK                      0
#define NEB_ERROR                   -1

#define NEB_TRUE                    1
#define NEB_FALSE                   0


/***** GENERIC ERRORS *****/
#define NEBERROR_NOMEM              100     /* memory could not be allocated */


/***** CALLBACK ERRORS *****/
#define NEBERROR_NOCALLBACKFUNC     200     /* no callback function was specified */
#define NEBERROR_NOCALLBACKLIST     201     /* callback list not initialized */
#define NEBERROR_CALLBACKBOUNDS     202     /* callback type was out of bounds */
#define NEBERROR_CALLBACKNOTFOUND   203     /* the callback could not be found */
#define NEBERROR_NOMODULEHANDLE     204     /* no module handle specified */
#define NEBERROR_BADMODULEHANDLE    205     /* bad module handle */
#define NEBERROR_CALLBACKOVERRIDE   206     /* module wants to override default Nagios handling of event */
#define NEBERROR_CALLBACKCANCEL     207     /* module wants to cancel callbacks to other modules */


/***** MODULE ERRORS *****/
#define NEBERROR_NOMODULE           300     /* no module was specified */


/***** MODULE INFO ERRORS *****/
#define NEBERROR_MODINFOBOUNDS      400     /* module info index was out of bounds */

#endif
