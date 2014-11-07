#ifndef _NM_ALLOC_H
#define _NM_ALLOC_H

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

void *nm_malloc(size_t size);
void *nm_realloc(void *ptr, size_t size);
void *nm_calloc(size_t count, size_t size);
void *nm_strdup(const char *s);
void *nm_strndup(const char *s, size_t size);
void nm_asprintf(char **strp, const char *fmt, ...);
#define nm_free(ptr) do { if(ptr) { free(ptr); ptr = NULL; } } while(0)
#endif
