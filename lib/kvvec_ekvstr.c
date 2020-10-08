#include <stdlib.h>
#include "kvvec_ekvstr.h"

/**
 * Table determines kind of escaping.
 * Unprintable and non-ASCII-values is generally escaped to \xNN (hex code),
 * which means value 1. printable chars is unescaped (0), other chars maps to
 * which char should follow the backslash
 */
static const char table_escape[256] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 116, 110, 1,
                                        1, 114, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        59, 0, 61, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 92, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1
                                      };

/**
 * Which character should be used if given the character after a backslash.
 *
 * \n maps ascii(n) = 110 to 10
 * \r maps ascii(r) = 114 to 13
 *
 * Exception for x, which is handled as a special case
 *
 * This should be mapped inverse to table_escape
 */
static const char table_unescape[256] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                          12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                                          30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
                                          0, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
                                          66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
                                          84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
                                          101, 102, 103, 104, 105, 106, 107, 108, 109, 10, 111, 112, 113, 13, 115,
                                          9, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
                                          131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144,
                                          145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158,
                                          159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172,
                                          173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186,
                                          187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200,
                                          201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214,
                                          215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228,
                                          229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242,
                                          243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
                                        };

/**
 * To unpack hex values. Given ascii value for a char, map it to the numeric
 * value of the char.
 * chars 0-9 maps to value 0-9
 * char a-f maps to value 10-15
 * char A-F maps to value 10-15
 * other values maps to 255, which is handled as errors.
 */
static const char table_dehex[256] = { 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 1, 2, 3,
                                       4, 5, 6, 7, 8, 9, 255, 255, 255, 255, 255, 255, 255, 10, 11, 12, 13, 14,
                                       15, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 10, 11,
                                       12, 13, 14, 15, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255
                                     };

/**
 * Table of how to format 0-15 in hex. Should match table_dehex
 */
static const char table_hex[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8',
                                    '9', 'a', 'b', 'c', 'd', 'e', 'f'
                                  };

static char *expect_string(const char **inptr, int *length, char endchar, char reserved);

char *kvvec_to_ekvstr(const struct kvvec *kvv)
{
	register unsigned char *buf;
	register unsigned char *ptr;
	register unsigned char *inptr;
	register unsigned char c;
	size_t bufsize;
	const struct key_value *kv;
	register int inlen;

	int i;
	register size_t j;

	/***********
	 * Counot number of bytes for the buffer
	 */
	bufsize = 1; /* Make sure null byte can be stored */
	for (i = 0; i < kvv->kv_pairs; i++) {
		kv = &kvv->kv[i];
		bufsize += 2;
		ptr = (unsigned char *) kv->key;
		for (j = kv->key_len; j; j--) {
			switch (table_escape[*(ptr++)]) {
			case 0:
				bufsize++;
				break;
			case 1:
				bufsize += 4;
				break;
			default:
				bufsize += 2;
				break;
			}
		}
		ptr = (unsigned char *) kv->value;
		for (j = kv->value_len; j; j--) {
			switch (table_escape[*(ptr++)]) {
			case 0:
				bufsize++;
				break;
			case 1:
				bufsize += 4;
				break;
			default:
				bufsize += 2;
				break;
			}
		}
	}

	/***********
	 * Allocate the buffer
	 */
	if ((buf = malloc(bufsize)) == NULL) {
		return NULL;
	}

	/***********
	 * Start packing
	 */
	ptr = buf;
	for (i = 0; i < kvv->kv_pairs; i++) {
		kv = &kvv->kv[i];

		inptr = (unsigned char *) kv->key;
		inlen = kv->key_len;

		while (inlen--) {
			c = table_escape[*inptr];
			if (c == 0) {
				*(ptr++) = *inptr;
			} else if (c == 1) {
				*(ptr++) = '\\';
				*(ptr++) = 'x';
				*(ptr++) = table_hex[*inptr >> 4];
				*(ptr++) = table_hex[*inptr & 0x0F];
			} else {
				*(ptr++) = '\\';
				*(ptr++) = c;
			}
			inptr++;
		}

		*(ptr++) = '=';

		inptr = (unsigned char *) kv->value;
		inlen = kv->value_len;

		while (inlen--) {
			c = table_escape[*inptr];
			if (c == 0) {
				*(ptr++) = *inptr;
			} else if (c == 1) {
				*(ptr++) = '\\';
				*(ptr++) = 'x';
				*(ptr++) = table_hex[*inptr >> 4];
				*(ptr++) = table_hex[*inptr & 0x0F];
			} else {
				*(ptr++) = '\\';
				*(ptr++) = c;
			}
			inptr++;
		}

		*(ptr++) = ';';
	}

	/* Handle empty vector */
	if (kvv->kv_pairs == 0)
		ptr++;

	*(--ptr) = '\0'; /* Overwrite last ; */
	return (char *) buf;
}

static char *expect_string(const char **inptr, int *length, char endchar, char reserved)
{
	char *buf = NULL;
	register const char *inp;
	register char *outp;
	register int len = 0;
	register char chr;
	register int v;

	inp = *inptr;
	len = 0;

	for (;;) {
		chr = *(inp++);
		if (chr == reserved)
			return NULL;
		if (chr == '\0')
			break;
		if (chr == endchar)
			break;
		if (chr == '\\') {
			chr = *(inp++);
			if (chr == 'x') {
				chr = *(inp++);
				if (chr == '\0')
					break;
				chr = *(inp++);
				if (chr == '\0')
					break;
			}
		}
		if (chr == '\0')
			break;
		len++;
	}
	len++;

	if ((buf = malloc(len)) == NULL) {
		return NULL;
	}

	inp = *inptr;
	outp = buf;
	for (;;) {
		chr = *(inp++);
		if (chr == '\0')
			break;
		if (chr == endchar)
			break;
		if (chr == '\\') {
			chr = *(inp++);
			if (chr == '\0') {
				break;
			} else if (chr != 'x') {
				*(outp++) = table_unescape[(unsigned char) chr];
			} else {
				chr = *(inp++);
				if (chr == '\0')
					break;
				v = table_dehex[(unsigned char) chr] << 4;
				chr = *(inp++);
				if (chr == '\0')
					break;
				v |= table_dehex[(unsigned char) chr];
				*(outp++) = v;
			}
		} else {
			*(outp++) = chr;
		}
		if (chr == '\0')
			break;
	}

	*inptr = inp - 1;
	*outp = '\0';
	*length = outp - buf;

	return buf;
}

#define FAIL_IF(_EXPR) do { if(_EXPR) { free(key); free(value); kvvec_destroy(kvv, KVVEC_FREE_ALL); return NULL; } } while(0)
struct kvvec *ekvstr_to_kvvec(const char *inbuf)
{
	struct kvvec *kvv = kvvec_create(35);

	const char *inptr = inbuf;
	char *key;
	char *value;
	int key_len;
	int value_len;

	while (*inptr) {
		key = value = NULL;
		key_len = value_len = 0;

		key = expect_string(&inptr, &key_len, '=', ';');
		FAIL_IF(key == NULL);
		FAIL_IF(*inptr != '=');

		inptr++;

		value = expect_string(&inptr, &value_len, ';', '=');
		FAIL_IF(value == NULL);

		kvvec_addkv_wlen(kvv, key, key_len, value, value_len);

		if (*inptr == ';')
			inptr++;
	}

	return kvv;
}
#undef FAIL_IF
