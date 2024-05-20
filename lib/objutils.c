#include "objutils.h"
#include <string.h>
#include <glib.h>
gboolean nm_service_equal(gconstpointer a, gconstpointer b)
{
	const nm_service_key *k1 = a, *k2 = b;
	if (!k1 || !k2)
		return (k1 == NULL && k2 == NULL);

	if (!g_str_equal(k1->hostname, k2->hostname))
		return FALSE;

	return g_str_equal(k1->service_description, k2->service_description);
}

guint nm_service_hash(gconstpointer key)
{
	const nm_service_key *k = key;
	return (g_str_hash(k->hostname) ^ g_str_hash(k->service_description));
}

nm_service_key *nm_service_key_create(const char *hostname, const char *service_description)
{
	nm_service_key *k = calloc(1, sizeof(*k));
	if (!k)
		return NULL;

	if ((k->hostname = strdup(hostname)) == NULL) {
		free(k);
		return NULL;
	}

	if ((k->service_description = strdup(service_description)) == NULL) {
		free(k->hostname);
		free(k);
		return NULL;
	}

	return k;
}

void nm_service_key_destroy(nm_service_key *k)
{
	free(k->hostname);
	free(k->service_description);
	free(k);
}
