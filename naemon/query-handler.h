#ifndef _QUERY_HANDLER_H
#define _QUERY_HANDLER_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

/* return codes for query_handlers() */
#define QH_OK        0  /* keep listening */
#define QH_CLOSE     1  /* we should close the socket */
#define QH_INVALID   2  /* invalid query. Log and close */
#define QH_TAKEOVER  3  /* handler will take full control. de-register but don't close */

NAGIOS_BEGIN_DECL

/*** Query Handler functions, types and macros*/
typedef int (*qh_handler)(int, char *, unsigned int);

int qh_init(const char *path);
void qh_deinit(const char *path);
int qh_register_handler(const char *name, const char *description, unsigned int options, qh_handler handler);
const char *qh_strerror(int code);
void qh_error(int sd, int code, const char *fmt, ...)
	__attribute__((__format__(__printf__, 3, 4)));

NAGIOS_END_DECL

#endif
