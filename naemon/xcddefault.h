#ifndef _XCDDEFAULT_H
#define _XCDDEFAULT_H
int xcddefault_initialize_comment_data(void);
int xcddefault_add_new_host_comment(int, char *, time_t, char *, char *, int, int, int, time_t, unsigned long *);
int xcddefault_add_new_service_comment(int, char *, char *, time_t, char *, char *, int, int, int, time_t, unsigned long *);
#endif
