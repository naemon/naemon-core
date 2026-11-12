#ifndef _NM_WRITEBUF_H
#define _NM_WRITEBUF_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lib/lnae-utils.h"
#include "objects_common.h"
#include <stddef.h>
#include <string.h>

NAGIOS_BEGIN_DECL

/*
 * Buffered writer for status.dat, retention.dat and objects.cache.
 *
 * These files used to be written with one fprintf() per field -- about 57 per
 * object in status.dat. Each of those parses its format string at runtime,
 * which made glibc's printf implementation half of the event loop's CPU time
 * once the object count got large. But they are almost entirely
 * integers and strings, and rendering those needs no format interpreter: when
 * the call site knows a field is an integer, it can convert it and append the
 * bytes directly.
 *
 * A nm_writebuf is a byte buffer plus a file descriptor. Appending copies to
 * buf + len; when the next field would not fit, the buffer goes out in a
 * single write(2) and len returns to 0. That is one syscall per megabyte
 * instead of thousands of small stdio writes.
 *
 * The per-field appenders are static inline here, so the compiler pastes them
 * into the writer loops instead of emitting a call per field. Only the cold
 * paths -- flushing, doubles, the printf fallback -- are real functions in
 * nm_writebuf.c.
 *
 *     nm_writebuf_init(&wb, fd, NM_WRITEBUF_SIZE);
 *     nm_wb_lit(&wb, "hoststatus {\n");
 *     nm_wb_kv_str(&wb, "\thost_name", hst->name);
 *     nm_wb_kv_int(&wb, "\tcurrent_state", hst->current_state);
 *     nm_wb_kv_dbl(&wb, "\tcheck_latency", "%.3f", hst->latency);
 *     nm_wb_lit(&wb, "\t}\n\n");
 *     if (nm_writebuf_done(&wb) != 0)
 *             ... a write() failed somewhere ...
 *
 * The key passed to the nm_wb_kv_* macros must be a string literal: the macro
 * pastes it together with "=" at compile time. It carries its own
 * indentation, because status.dat indents fields inside a block and
 * retention.dat does not.
 *
 * Write errors are not reported per call -- checking a return value on every
 * append would be noise. A failed write() sets a sticky flag, appending
 * continues harmlessly, and nm_writebuf_done() reports it once at the end,
 * which is where the old code checked ferror(). The caller must not rename
 * the temp file over the real one when that comes back non-zero.
 */

struct nm_writebuf {
	char *buf;
	size_t len;
	size_t cap;
	int fd;
	int error; /* sticky: set when a write() failed */
};

#define NM_WRITEBUF_SIZE (1024 * 1024)

/* Allocates the buffer. Call nm_writebuf_done() to flush and release it. */
void nm_writebuf_init(struct nm_writebuf *wb, int fd, size_t size);

/* Flushes the remaining bytes and frees the buffer. Returns 0 on success. */
int nm_writebuf_done(struct nm_writebuf *wb);

/* Writes the buffer out and empties it. */
void nm_writebuf_flush(struct nm_writebuf *wb);

/* Grows the buffer so a single oversized field fits. */
void nm_writebuf_grow(struct nm_writebuf *wb, size_t need);

/*
 * Make room for n more bytes. If they do not fit, the buffer is written out
 * and emptied first. The second check only fires for a single field that is
 * larger than the whole buffer (a very long plugin output, say), in which case
 * the buffer is grown to fit it.
 */
static inline void nm_wb_reserve(struct nm_writebuf *wb, size_t n)
{
	if (wb->len + n > wb->cap)
		nm_writebuf_flush(wb);
	if (n > wb->cap)
		nm_writebuf_grow(wb, n);
}

/* Appends n bytes. This is what every other appender ends up calling. */
static inline void nm_wb_mem(struct nm_writebuf *wb, const char *s, size_t n)
{
	nm_wb_reserve(wb, n);
	memcpy(wb->buf + wb->len, s, n);
	wb->len += n;
}

/*
 * Appends a string literal. sizeof(s) - 1 is its length minus the terminating
 * NUL, computed by the compiler, so this costs no strlen() at runtime. Only
 * works for literals -- for a char* use nm_wb_str().
 */
#define nm_wb_lit(wb, s) nm_wb_mem((wb), (s), sizeof(s) - 1)

/*
 * Appends a C string. A NULL string appends nothing, which is what the
 * (x == NULL) ? "" : x fallbacks in the old fprintf() calls did.
 */
static inline void nm_wb_str(struct nm_writebuf *wb, const char *s)
{
	if (s != NULL)
		nm_wb_mem(wb, s, strlen(s));
}

/*
 * Appends an unsigned integer in decimal, no padding. Digits come out least
 * significant first, so they are written backwards into a scratch buffer and
 * the filled tail is appended. The do/while runs at least once, so 0 produces
 * "0"; 24 bytes covers any 64 bit value.
 */
static inline void nm_wb_uint(struct nm_writebuf *wb, unsigned long long v)
{
	char tmp[24];
	int i = (int)sizeof(tmp);

	do {
		tmp[--i] = (char)('0' + (v % 10));
		v /= 10;
	} while (v);
	nm_wb_mem(wb, tmp + i, sizeof(tmp) - (size_t)i);
}

/* Appends a signed integer in decimal. */
static inline void nm_wb_int(struct nm_writebuf *wb, long long v)
{
	if (v < 0) {
		nm_wb_lit(wb, "-");
		/*
		 * Negating LLONG_MIN is undefined behaviour -- its absolute value does
		 * not fit in a long long. Adding 1 first, negating, then adding it back
		 * in unsigned arithmetic gets the magnitude without overflowing.
		 */
		nm_wb_uint(wb, (unsigned long long) - (v + 1) + 1ULL);
	} else {
		nm_wb_uint(wb, (unsigned long long)v);
	}
}

/*
 * Appends a double, formatted with `fmt` (one of "%f", "%.3f", "%.2f").
 * Doubles still go through printf: it rounds the exact value of the binary
 * double, and a hand written version disagreeing in a last-digit tie case
 * would silently change what lands in the file.
 */
void nm_wb_dbl(struct nm_writebuf *wb, const char *fmt, double v);

/* Fallback for the few lines that are neither a plain integer nor a string. */
void nm_wb_printf(struct nm_writebuf *wb, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

/*
 * "key=value\n" helpers. key is always a string literal, and carries its own
 * indentation where the file format has any -- status.dat indents its fields
 * with a tab, retention.dat does not.
 */
#define nm_wb_kv_str(wb, key, val)   do { nm_wb_lit((wb), key "="); nm_wb_str((wb), (val)); nm_wb_lit((wb), "\n"); } while (0)
#define nm_wb_kv_int(wb, key, val)   do { nm_wb_lit((wb), key "="); nm_wb_int((wb), (long long)(val)); nm_wb_lit((wb), "\n"); } while (0)
#define nm_wb_kv_uint(wb, key, val)  do { nm_wb_lit((wb), key "="); nm_wb_uint((wb), (unsigned long long)(val)); nm_wb_lit((wb), "\n"); } while (0)
#define nm_wb_kv_dbl(wb, key, fmt, val) do { nm_wb_lit((wb), key "="); nm_wb_dbl((wb), (fmt), (double)(val)); nm_wb_lit((wb), "\n"); } while (0)

/* "<prefix>_NAME=MODIFIED;VALUE\n" -- both files write custom variables. */
void nm_wb_customvar(struct nm_writebuf *wb, const char *prefix,
                     const customvariablesmember *cv);

NAGIOS_END_DECL

#endif
