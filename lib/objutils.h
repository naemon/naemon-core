#ifndef LIBNAEMON_objutils_h__
#define LIBNAEMON_objutils_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lnae-utils.h"

NAEMON_BEGIN_DECL

typedef struct nm_service_key {
	char *hostname;
	char *service_description;
} nm_service_key;

int nm_service_equal(const void *a, const void *b);
unsigned int nm_service_hash(const void *key);

nm_service_key * nm_service_key_create(const char *hostname, const char *service_description);
void nm_service_key_destroy(nm_service_key *k);

NAEMON_END_DECL
#endif
