#include <string.h>
#include <stdarg.h>
#include "logging.h"
#include "nm_alloc.h"

#ifndef __func__
# if __STDC_VERSION__ < 199901L
#  if __GNUC__ >= 2
#   define __func__ __FUNCTION__
#  else
#   define __func__ "<unknown>"
#  endif
# endif
#endif
#define log_mem_error() nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to allocate memory in %s", __func__)
#define log_vasprintf_error() nm_log(NSLOG_RUNTIME_ERROR, "Error: Failed to vasprintf in %s", __func__)

#define CHECK_AND_RETURN(_ptr)					\
	if (_ptr == NULL) {							\
		log_mem_error();						\
		exit(2);								\
	}											\
	return _ptr;

void *nm_malloc(size_t size) {
	void *ptr = malloc(size);
	CHECK_AND_RETURN(ptr);
}

void *nm_realloc(void *ptr, size_t size)  {
	void *new_ptr = realloc(ptr, size);
	CHECK_AND_RETURN(new_ptr);
}

void *nm_calloc(size_t count, size_t size) {
	void *ptr = calloc(count, size);
	CHECK_AND_RETURN(ptr);
}

void *nm_strdup(const char *s) {
	char *str = strdup(s);
	CHECK_AND_RETURN(str);
}

void *nm_strndup(const char *s, size_t size) {
	char *str = strndup(s, size);
	CHECK_AND_RETURN(str);
}

void nm_asprintf(char **strp, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	if (vasprintf(strp, fmt, ap) < 0) {
		log_vasprintf_error();
		exit(2);
	}
	va_end(ap);
}
