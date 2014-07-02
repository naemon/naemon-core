#ifndef _NM_ALLOC_H
#define _NM_ALLOC_H
void *nm_malloc(size_t size);
void *nm_realloc(void *ptr, size_t size);
void *nm_calloc(size_t count, size_t size);
void *nm_strdup(const char *s);
void *nm_strndup(const char *s, size_t size);
void nm_asprintf(char **strp, const char *fmt, ...);
#endif
