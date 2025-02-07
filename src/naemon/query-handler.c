#include "config.h"
#include "lib/libnaemon.h"
#include "lib/nsock.h"
#include "query-handler.h"
#include "events.h"
#include "utils.h"
#include "logging.h"
#include "globals.h"
#include "commands.h"
#include "nm_alloc.h"
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>

/* A registered handler */
struct query_handler {
	const char *name; /* also "address" of this handler. Must be unique */
	const char *description; /* short description of this handler */
	unsigned int options;
	qh_handler handler;
	struct query_handler *prev_qh, *next_qh;
};

static struct query_handler *qhandlers;
static int qh_listen_sock = -1; /* the listening socket */
static unsigned int qh_running;
unsigned int qh_max_running = 0; /* defaults to unlimited */
static GHashTable *qh_table;

/* the echo service. stupid, but useful for testing */
static int qh_echo(int sd, char *buf, unsigned int len)
{
	if (!strcmp(buf, "help")) {
		nsock_printf_nul(sd,
		                 "Query handler that simply echoes back what you send it.");
		return 0;
	}
	return nsock_write_all(sd, buf, len);
}

static struct query_handler *qh_find_handler(const char *name)
{
	return g_hash_table_lookup(qh_table, name);
}

/* subset of http error codes */
const char *qh_strerror(int code)
{
	if (code < 0)
		return "Low-level system error";

	if (code == 100)
		return "Continue";
	if (code == 101)
		return "Switching protocols";

	if (code < 300)
		return "OK";

	if (code < 400)
		return "Redirected (possibly deprecated address)";

	switch (code) {
	/* client errors */
	case 400: return "Bad request";
	case 401: return "Unathorized";
	case 403: return "Forbidden (disabled by config)";
	case 404: return "Not found";
	case 405: return "Method not allowed";
	case 406: return "Not acceptable";
	case 407: return "Proxy authentication required";
	case 408: return "Request timed out";
	case 409: return "Conflict";
	case 410: return "Gone";
	case 411: return "Length required";
	case 412: return "Precondition failed";
	case 413: return "Request too large";
	case 414: return "Request-URI too long";

	/* server errors */
	case 500: return "Internal server error";
	case 501: return "Not implemented";
	case 502: return "Bad gateway";
	case 503: return "Service unavailable";
	case 504: return "Gateway timeout";
	case 505: return "Version not supported";
	}
	return "Unknown error";
}

static int qh_input(int sd, int events, void *bq_)
{
	nm_bufferqueue *bq = (nm_bufferqueue *)bq_;
	int result;
	size_t len;
	unsigned int query_len = 0;
	char *buf, *space;
	struct query_handler *qh;
	char *handler = NULL, *query = NULL;

	result = nm_bufferqueue_read(bq, sd);
	/* disconnect? */
	if (result == 0 || (result < 0 && errno == EPIPE)) {
		nm_bufferqueue_destroy(bq);
		iobroker_close(nagios_iobs, sd);
		qh_running--;
		return 0;
	}

	/*
	 * A request looks like this: '[@|#]<qh>[<SP>][<query>]\0'.
	 * That is, optional '#' (oneshot) or '@' (keepalive),
	 * followed by the name of a registered handler, followed by
	 * an optional space and an optional query. If the handler
	 * has no "default" handler, a query is required or an error
	 * will be thrown.
	 */

	/* Use data up to the first nul byte */
	nm_bufferqueue_unshift_to_delim(bq, "\0", 1, &len, (void **)&buf);
	if (!buf)
		return 0;

	/* Identify handler part and any magic query bytes */
	if (*buf == '@' || *buf == '#') {
		handler = buf + 1;
	} else {
		handler = buf;
	}

	/* Locate query (if any) */
	if ((space = strchr(buf, ' '))) {
		*space = 0;
		query = space + 1;
		query_len = len - ((unsigned long)query - (unsigned long)buf);
	} else {
		query = "";
		query_len = 0;
	}

	/* locate the handler */
	if (!(qh = qh_find_handler(handler))) {
		/* not found. that's a 404 */
		nsock_printf(sd, "404: %s: No such handler", handler);
		nm_free(buf);
		iobroker_close(nagios_iobs, sd);
		nm_bufferqueue_destroy(bq);
		return 0;
	}

	/* strip trailing newlines */
	while (query_len > 0 && (query[query_len - 1] == 0 || query[query_len - 1] == '\n'))
		query[--query_len] = 0;

	/* now pass the query to the handler */
	if ((result = qh->handler(sd, query, query_len)) >= 100) {
		nsock_printf_nul(sd, "%d: %s", result, qh_strerror(result));
	}

	if (result >= 300 || *buf != '@') {
		/* error code or one-shot query */
		nm_free(buf);
		iobroker_close(nagios_iobs, sd);
		nm_bufferqueue_destroy(bq);
		return 0;
	}
	nm_free(buf);

	/* check for magic handler codes */
	switch (result) {
	case QH_CLOSE: /* oneshot handler */
	case -1:       /* general error */
		iobroker_close(nagios_iobs, sd);
	/* fallthrough */
	case QH_TAKEOVER: /* handler takes over */
	case 101:         /* switch protocol (takeover + message) */
		nm_bufferqueue_destroy(bq);
		break;
	}
	return 0;
}

static int qh_registration_input(int sd, int events, void *bq_)
{
	nm_bufferqueue *bq = (nm_bufferqueue *)bq_;
	struct sockaddr sa;
	socklen_t slen = 0;
	int nsd, result;

	memset(&sa, 0, sizeof(sa)); /* shut valgrind up */
	nsd = accept(sd, &sa, &slen);
	if (qh_max_running && qh_running >= qh_max_running) {
		nsock_printf(nsd, "503: Server full");
		close(nsd);
		return 0;
	}

	if (!(bq = nm_bufferqueue_create())) {
		nm_log(NSLOG_RUNTIME_ERROR, "qh: Failed to create iocache for inbound request\n");
		nsock_printf(nsd, "500: Internal server error");
		close(nsd);
		return 0;
	}

	/*
	 * @todo: Stash the iocache and the socket in some
	 * addressable list so we can release them on deinit
	 */
	result = iobroker_register(nagios_iobs, nsd, bq, qh_input);
	if (result < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "qh: Failed to register input socket %d with I/O broker: %s; errno=%d (%s)\n",
		       nsd, iobroker_strerror(result), errno, strerror(errno));
		nm_bufferqueue_destroy(bq);
		close(nsd);
		return 0;
	}

	/* make it non-blocking, but leave kernel buffers unchanged */
	worker_set_sockopts(nsd, 0);
	qh_running++;
	return 0;
}

static void qh_remove(struct query_handler *qh)
{
	struct query_handler *next, *prev;
	if (!qh)
		return;

	next = qh->next_qh;
	prev = qh->prev_qh;
	if (next)
		next->prev_qh = prev;
	if (prev)
		prev->next_qh = next;
	else
		qhandlers = next;

	nm_free(qh);
}

int qh_deregister_handler(const char *name)
{
	g_hash_table_remove(qh_table, name);
	return 0;
}

int qh_register_handler(const char *name, const char *description, unsigned int options, qh_handler handler)
{

	struct query_handler *qh;

	g_return_val_if_fail(qh_table != NULL, -1);
	g_return_val_if_fail(name != NULL, -1);

	if (!handler) {
		nm_log(NSLOG_RUNTIME_ERROR, "qh: Failed to register handler '%s': No handler function specified\n", name);
		return -1;
	}

	if (strlen(name) > 128) {
		nm_log(NSLOG_RUNTIME_ERROR, "qh: Failed to register handler '%s': Name too long\n", name);
		return -ENAMETOOLONG;
	}

	/* names must be unique */
	if (qh_find_handler(name)) {
		nm_log(NSLOG_RUNTIME_WARNING, "qh: Handler '%s' registered more than once\n", name);
		return -1;
	}

	qh = nm_calloc(1, sizeof(*qh));
	qh->name = name;
	qh->description = description;
	qh->handler = handler;
	qh->options = options;
	qh->next_qh = qhandlers;
	if (qhandlers)
		qhandlers->prev_qh = qh;
	qhandlers = qh;

	g_hash_table_insert(qh_table, nm_strdup(qh->name), qh);

	return 0;
}

void qh_deinit(const char *path)
{
	g_hash_table_destroy(qh_table);
	qh_table = NULL;
	qhandlers = NULL;

	if (path)
		unlink(path);
}

static int qh_help(int sd, char *buf, unsigned int len)
{
	struct query_handler *qh;

	if (!*buf || !strcmp(buf, "help")) {
		nsock_printf_nul(sd,
		                 "  help <name>   show help for handler <name>\n"
		                 "  help list     list registered handlers\n");
		return 0;
	}

	if (!strcmp(buf, "list")) {
		for (qh = qhandlers; qh; qh = qh->next_qh) {
			nsock_printf(sd, "%-10s %s\n", qh->name, qh->description ? qh->description : "(No description available)");
		}
		nsock_printf(sd, "%c", 0);
		return 0;
	}

	if (!(qh = qh_find_handler(buf))) {
		nsock_printf_nul(sd, "No handler named '%s' is registered\n", buf);
	} else if (qh->handler(sd, "help", 4) > 200) {
		nsock_printf_nul(sd, "The handler %s doesn't have any help yet.", buf);
	}

	return 0;
}

static int qh_command(int sd, char *buf, unsigned int len)
{
	char *space;
	int mode;

	if (!*buf || !strcmp(buf, "help")) {
		nsock_printf_nul(sd, "Query handler for naemon commands.\n"
		                 "Available commands:\n"
		                 "  run <command>     Run a command\n"
		                 "  runkv <command>   Run a command as escaped kvvec\n"
		                );
		return 0;
	}
	if ((space = memchr(buf, ' ', len)))
		* (space++) = 0;
	if (space) {
		mode = 0;
		if (!strcmp(buf, "run")) {
			mode = COMMAND_SYNTAX_NOKV;
		} else if (!strcmp(buf, "runkv")) {
			mode = COMMAND_SYNTAX_KV;
		}
		if (mode != 0) {
			GError *error = NULL;
			int res = process_external_command(space, mode, &error);
			if (res == OK) {
				return 200;
			} else {
				nsock_printf_nul(sd, "400: %s\n", error->message);
				g_clear_error(&error);
				return 0;
			}
		}
	}

	return 404;
}

int qh_init(const char *path)
{
	int result, old_umask;

	if (qh_listen_sock >= 0)
		iobroker_close(nagios_iobs, qh_listen_sock);

	if (!path) {
		nm_log(NSLOG_RUNTIME_ERROR, "qh: query_socket is NULL. What voodoo is this?\n");
		return ERROR;
	}

	old_umask = umask(0117);
	errno = 0;
	qh_listen_sock = nsock_unix(path, NSOCK_TCP | NSOCK_UNLINK);
	umask(old_umask);
	if (qh_listen_sock < 0) {
		nm_log(NSLOG_RUNTIME_ERROR, "qh: Failed to init socket '%s'. %s: %s\n",
		       path, nsock_strerror(qh_listen_sock), strerror(errno));
		return ERROR;
	}

	/* plugins shouldn't have this socket */
	(void)fcntl(qh_listen_sock, F_SETFD, FD_CLOEXEC);

	/* most likely overkill, but it's small, so... */
	qh_table = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                 free, (GDestroyNotify) qh_remove);
	errno = 0;
	result = iobroker_register(nagios_iobs, qh_listen_sock, NULL, qh_registration_input);
	if (result < 0) {
		g_hash_table_destroy(qh_table);
		qh_close_socket();
		nm_log(NSLOG_RUNTIME_ERROR, "qh: Failed to register socket with io broker: %s\n", iobroker_strerror(result));
		return ERROR;
	}

	nm_log(NSLOG_INFO_MESSAGE, "qh: Socket '%s' successfully initialized\n", path);

	/* now register our the in-core handlers */
	qh_register_handler("command", "Naemon external commands interface", 0, qh_command);
	qh_register_handler("echo", "The Echo Service - What You Put Is What You Get", 0, qh_echo);
	qh_register_handler("help", "Help for the query handler", 0, qh_help);

	return 0;
}

void qh_close_socket() {
	if( qh_listen_sock > 0 )
		close(qh_listen_sock);
	qh_listen_sock = -1;
}