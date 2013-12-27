#ifndef _QUERY_HANDLER_H
#define _QUERY_HANDLER_H


/* return codes for query_handlers() */
#define QH_OK        0  /* keep listening */
#define QH_CLOSE     1  /* we should close the socket */
#define QH_INVALID   2  /* invalid query. Log and close */
#define QH_TAKEOVER  3  /* handler will take full control. de-register but don't close */

/*** Query Handler functions, types and macros*/
typedef int (*qh_handler)(int, char *, unsigned int);


extern int qh_init(const char *path);
extern void qh_deinit(const char *path);
extern int qh_register_handler(const char *name, const char *description, unsigned int options, qh_handler handler);
extern const char *qh_strerror(int code);

#endif
