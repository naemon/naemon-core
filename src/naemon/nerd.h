#ifndef _NERD_H
#define _NERD_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

NAGIOS_BEGIN_DECL

/** Nerd subscription type */
struct nerd_subscription {
	int sd;
	struct nerd_channel *chan;
	char *format; /* requested format (macro string) for this subscription */
};

/*** Nagios Event Radio Dispatcher functions ***/
int nerd_init(void);
int nerd_mkchan(const char *name, const char *description, int (*handler)(int, void *), unsigned int callbacks);
int nerd_cancel_subscriber(int sd);
int nerd_get_channel_id(const char *chan_name);
objectlist *nerd_get_subscriptions(int chan_id);
int nerd_broadcast(unsigned int chan_id, void *buf, unsigned int len);

NAGIOS_END_DECL

#endif
