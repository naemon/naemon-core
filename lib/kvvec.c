/*
 * key+value vector library
 *
 * Small and simple, but pretty helpful when parsing configurations
 * from random formats into something a program can easily make sense
 * of.
 *
 * The main type (struct kvvec *) should possibly be opaque since
 * all callers should use the kvvec_foreach() variable to trudge
 * around in the key/value vector.
 */
#include <stdlib.h>
#include <string.h>
#include "kvvec.h"
#include "nsutils.h"

struct kvvec *kvvec_init(struct kvvec *kvv, int hint)
{
	if (!kvv)
		return NULL;

	kvv->kv_pairs = 0;
	if (kvv->kv_alloc < hint && kvvec_resize(kvv, hint) < 0)
		return NULL;

	return kvv;
}

struct kvvec *kvvec_create(int hint)
{
	struct kvvec *kvv = calloc(1, sizeof(*kvv));
	if (kvv && !kvvec_init(kvv, hint)) {
		free(kvv);
		return NULL;
	}
	return kvv;
}

int kvvec_resize(struct kvvec *kvv, int hint)
{
	struct key_value *kv;

	if (!kvv)
		return -1;

	if (hint <= kvv->kv_alloc)
		return 0;

	kv = realloc(kvv->kv, sizeof(struct key_value) * hint);
	if (!kv)
		return -1;

	memset(&kv[kvv->kv_alloc], 0, (hint - kvv->kv_alloc) * sizeof(*kv));
	kvv->kv = kv;
	kvv->kv_alloc = hint;
	return 0;
}

int kvvec_grow(struct kvvec *kvv, int hint)
{
	if (!kvv)
		return -1;

	/* grow reasonably when we don't have a hint */
	if (!hint)
		hint = (kvv->kv_alloc / 3) + 15;

	return kvvec_resize(kvv, kvv->kv_alloc + hint);
}

int kvvec_addkv_long(struct kvvec *kvv, const char *key, long value)
{
	const char *buf = mkstr("%ld", value);
	return kvvec_addkv_wlen(kvv, key, strlen(key), buf, strlen(buf));
}

int kvvec_addkv_tv(struct kvvec *kvv, const char *key, const struct timeval *value)
{
	const char *buf = mkstr("%ld.%06ld", (long)value->tv_sec, (long)value->tv_usec);
	return kvvec_addkv_wlen(kvv, key, strlen(key), buf, strlen(buf));
}

int kvvec_addkv_str(struct kvvec *kvv, const char *key, const char *value)
{
	return kvvec_addkv_wlen(kvv, key, strlen(key), value, strlen(value));
}

int kvvec_addkv_wlen(struct kvvec *kvv, const char *key, int keylen, const char *value, int valuelen)
{
	struct key_value *kv;

	if (!kvv || !key)
		return -1;

	if (kvv->kv_pairs >= kvv->kv_alloc - 1) {
		if (kvvec_grow(kvv, 0))
			return -1;
	}

	kv = &kvv->kv[kvv->kv_pairs++];
	kv->key = (char *)key;
	kv->key_len = keylen;
	kv->value = (char *)value;
	kv->value_len = valuelen;

	kvv->kvv_sorted = 0;

	return 0;
}

static int val_compare(const void *a, int a_len, const void *b, int b_len)
{
	size_t prefix;
	int diff;

	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL)
		return -1;
	if (b == NULL)
		return 1;

	if (a_len > b_len)
		prefix = b_len;
	else
		prefix = a_len;

	diff = memcmp(a, b, prefix);
	if (diff != 0)
		return diff;
	return a_len - b_len;
}

static int kv_compare(const void *a_, const void *b_)
{
	const struct key_value *a = (const struct key_value *)a_;
	const struct key_value *b = (const struct key_value *)b_;
	int ret = 0;

	ret = val_compare(a->key, a->key_len, b->key, b->key_len);

	if (ret != 0)
		return ret;

	return val_compare(a->value, a->value_len, b->value, b->value_len);
}

int kvvec_sort(struct kvvec *kvv)
{
	qsort(kvv->kv, kvv->kv_pairs, sizeof(struct key_value), kv_compare);
	kvv->kvv_sorted = 1;
	return 0;
}

char *kvvec_fetch_str_str(struct kvvec *kvv, const char *key)
{
	struct key_value *val = kvvec_fetch(kvv, key, strlen(key));
	if (val)
		return val->value;
	return NULL;
}

struct key_value *kvvec_fetch(struct kvvec *kvv, const char *key, int keylen)
{
	int i;

	/* If sorted, do a binary search */
	if (kvv->kvv_sorted) {
		int low = 0;
		int high = kvv->kv_pairs;
		while(low < high) {
			int mid = (low + high)/2;
			struct key_value *midkv = &(kvv->kv[mid]);

			int diff = val_compare(midkv->key, midkv->key_len, key, keylen);

			if (diff > 0) {
				high = mid;
			} else if (diff < 0) {
				low = mid+1;
			} else {
				return midkv;
			}

		}
		return NULL;
	}

	/* If unsorted, lookup until found. O(n) */
	for (i=0;i<kvv->kv_pairs;i++) {
		if (keylen != kvv->kv[i].key_len)
			continue;
		if (memcmp(kvv->kv[i].key, key, keylen) != 0)
			continue;
		return &(kvv->kv[i]);
	}

	/* Nothing is found, return NULL */
	return NULL;
}

int kvvec_foreach(struct kvvec *kvv, void *arg, int (*callback)(struct key_value *, void *))
{
	int i;

	if (!kvv)
		return 0;

	for (i = 0; i < kvv->kv_pairs; i++) {
		callback(&kvv->kv[i], arg);
	}
	return 0;
}

void kvvec_free_kvpairs(struct kvvec *kvv, int flags)
{
	int i;

	if (flags == KVVEC_FREE_ALL) {
		for (i = 0; i < kvv->kv_pairs; i++) {
			free(kvv->kv[i].key);
			free(kvv->kv[i].value);
		}
	} else if (flags == KVVEC_FREE_KEYS) {
		for (i = 0; i < kvv->kv_pairs; i++) {
			free(kvv->kv[i].key);
		}
	} else if (flags == KVVEC_FREE_VALUES) {
		for (i = 0; i < kvv->kv_pairs; i++) {
			free(kvv->kv[i].value);
		}
	}

	kvv->kv_pairs = 0;
}


int kvvec_destroy(struct kvvec *kvv, int flags)
{
	if(kvv == NULL)
		return 0;
	kvvec_free_kvpairs(kvv, flags);
	free(kvv->kv);
	free(kvv);
	return 0;
}

/*
 * Caller can tell us to over-allocate the buffer if he/she wants
 * to put extra stuff at the end of it.
 */
struct kvvec_buf *kvvec2buf(struct kvvec *kvv, char kv_sep, char pair_sep, int overalloc)
{
	struct kvvec_buf *kvvb;
	int i;
	unsigned long len = 0;

	if (!kvv)
		return NULL;

	kvvb = malloc(sizeof(struct kvvec_buf));
	if (!kvvb)
		return NULL;

	/* overalloc + (kv_sep_size * kv_pairs) + (pair_sep_size * kv_pairs) */
	kvvb->bufsize = overalloc + (kvv->kv_pairs * 2);
	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv = &kvv->kv[i];
		kvvb->bufsize += kv->key_len + kv->value_len;
	}

	kvvb->buf = malloc(kvvb->bufsize);
	if (!kvvb->buf) {
		free(kvvb);
		return NULL;
	}

	for (i = 0; i < kvv->kv_pairs; i++) {
		struct key_value *kv = &kvv->kv[i];
		memcpy(kvvb->buf + len, kv->key, kv->key_len);
		len += kv->key_len;
		kvvb->buf[len++] = kv_sep;
		if (kv->value_len) {
			memcpy(kvvb->buf + len, kv->value, kv->value_len);
			len += kv->value_len;
		}
		kvvb->buf[len++] = pair_sep;
	}
	memset(kvvb->buf + len, 0, kvvb->bufsize - len);
	kvvb->buflen = len;
	return kvvb;
}

unsigned int kvvec_capacity(struct kvvec *kvv)
{
	if (!kvv)
		return 0;

	return kvv->kv_alloc - kvv->kv_pairs;
}

/*
 * Converts a buffer of random bytes to a key/value vector.
 * This requires a fairly rigid format in the input data to be of
 * much use, but it's nifty for ipc where only computers are
 * involved, and it will parse the kvvec2buf() produce nicely.
 */
int buf2kvvec_prealloc(struct kvvec *kvv, char *str,
                       unsigned int len, const char kvsep,
                       const char pair_sep, int flags)
{
	unsigned int num_pairs = 0, i, offset = 0;

	if (!str || !len || !kvv)
		return -1;

	/* first we count the number of key/value pairs */
	while (offset < len) {
		const char *ptr;

		/* keys can't start with nul bytes */
		if (*(str + offset)) {
			num_pairs++;
		}

		ptr = memchr(str + offset, pair_sep, len - offset);
		ptr++;
		if (!ptr)
			break;
		offset += (unsigned long)ptr - ((unsigned long)str + offset);
	}

	if (!num_pairs) {
		return 0;
	}

	/* make sure the key/value vector is large enough */
	if (!(flags & KVVEC_APPEND)) {
		kvvec_init(kvv, num_pairs);
	} else if (kvvec_capacity(kvv) < num_pairs && kvvec_resize(kvv, num_pairs) < 0) {
		return -1;
	}

	offset = 0;
	for (i = 0; i < num_pairs; i++) {
		struct key_value *kv;
		char *key_end_ptr, *kv_end_ptr;

		/* keys can't begin with nul bytes */
		if (offset && str[offset] == '\0') {
			return kvv->kv_pairs;
		}

		key_end_ptr = memchr(str + offset, kvsep, len - offset);
		if (!key_end_ptr) {
			break;
		}
		kv_end_ptr = memchr(key_end_ptr + 1, pair_sep, len - ((unsigned long)key_end_ptr - (unsigned long)str));
		if (!kv_end_ptr) {
			if (i != num_pairs - 1)
				break;
			/* last pair doesn't need a pair separator */
			kv_end_ptr = str + len;
		}

		kv = &kvv->kv[kvv->kv_pairs++];
		kv->key_len = (unsigned long)key_end_ptr - ((unsigned long)str + offset);
		if (flags & KVVEC_COPY) {
			kv->key = malloc(kv->key_len + 1);
			memcpy(kv->key, str + offset, kv->key_len);
		} else {
			kv->key = str + offset;
		}
		kv->key[kv->key_len] = 0;

		offset += kv->key_len + 1;

		if (str[offset] == pair_sep) {
			kv->value_len = 0;
			if (flags & KVVEC_COPY) {
				kv->value = strdup("");
			} else {
				kv->value = (char *)"";
			}
		} else {
			kv->value_len = (unsigned long)kv_end_ptr - ((unsigned long)str + offset);
			if (flags & KVVEC_COPY) {
				kv->value = malloc(kv->value_len + 1);
				memcpy(kv->value, str + offset, kv->value_len);
			} else {
				kv->value = str + offset;
			}
			kv->value[kv->value_len] = 0;
		}

		offset += kv->value_len + 1;
	}

	return i;
}

struct kvvec *buf2kvvec(char *str, unsigned int len, const char kvsep,
                        const char pair_sep, int flags)
{
	struct kvvec *kvv;

	kvv = kvvec_create(len / 20);
	if (!kvv)
		return NULL;

	if (buf2kvvec_prealloc(kvv, str, len, kvsep, pair_sep, flags) >= 0)
		return kvv;

	free(kvv);
	return NULL;
}
