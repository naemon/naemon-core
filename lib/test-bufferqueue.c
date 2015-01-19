#include <stdio.h>
#include <stdarg.h>
#include "bufferqueue.c"
#include "t-utils.h"

struct strcode {
	char *str;
	size_t len;
};
#define ADDSTR(str) { str, sizeof(str) - 1 }
static int test_delimiter(const char *delim, size_t delim_len)
{
	struct strcode sc[] = {
		ADDSTR("Charlie Chaplin"),
		ADDSTR("Madonna Something something"),
		ADDSTR("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nulla turpis augue, laoreet eleifend ultricies et, tincidunt non felis. Suspendisse vitae accumsan dolor. Vivamus posuere venenatis dictum. Integer hendrerit est eget turpis scelerisque porttitor. Donec ullamcorper sodales purus, sed bibendum odio porttitor sit amet. Donec pretium sem ac sapien iaculis feugiat. Quisque commodo consequat quam, ac cursus est sodales euismod. Sed nec massa felis, sit amet varius dui. Morbi fermentum varius tellus, eget tempus felis imperdiet quis. Praesent congue auctor ligula, a tempor ipsum malesuada at. Proin pharetra tempor adipiscing. Aenean egestas tellus vitae arcu sagittis non ultrices turpis cursus."),
		ADDSTR("Emma Blomqvist"),
		ADDSTR("Random message"),
		ADDSTR("Random\0message\0with\0nuls\0embedded"),
		{ NULL, 0, },
	};
	int i;
	nm_bufferqueue *bq;

	bq = nm_bufferqueue_create();
	if (!test(bq != NULL, "nm_bufferqueue_create must work"))
		crash("can't test with no available memory");

	t_start("Testing delimiter '%s' of len %ld at start of block", delim, delim_len);
	for (i = 0; sc[i].str; i++) {
		nm_bufferqueue_push(bq, sc[i].str, sc[i].len);
		nm_bufferqueue_push(bq, delim, delim_len);
		test(!nm_bufferqueue_peek(bq, sc[i].len, NULL), "peek should succeed when it reads less than is in the buffer");
		test(!nm_bufferqueue_peek(bq, delim_len, NULL), "peek should succeed when it reads less than is in the buffer");
	}

	for (i = 0; sc[i].str; i++) {
		char *ptr = NULL;
		unsigned long len = 0;
		test(nm_bufferqueue_get_available(bq) >= sc[i].len + delim_len, "There should be data left");
		nm_bufferqueue_unshift_to_delim(bq, delim, delim_len, &len, (void **)&ptr);
		t_req(ptr != NULL);
		test(len == sc[i].len + delim_len, "len check, delim '%s' on string %d(%s), expected %ld, was %ld", delim, i, sc[i].str, sc[i].len + delim_len, len);
		test(!memcmp(ptr, sc[i].str, len - delim_len), "memcmp() check, delim '%s' on string %d(%s), expected '%.*s', was '%.*s'", delim, i, sc[i].str, (int) len, sc[i].str, (int) len, ptr - len);
		test(!memcmp(ptr + sc[i].len, delim, delim_len), "memcmp() check, delim '%s' on string %d(%s), expected '%.*s', was '%.*s'", delim, i, sc[i].str, (int) len, sc[i].str, (int) len, ptr - len);
		free(ptr);
	}
	t_end();

	t_start("Testing delimiter '%s' of len %ld at end of block", delim, delim_len);
	for (i = 0; sc[i].str; i++) {
		char concatenated[sc[i].len + delim_len];
		memcpy(concatenated, sc[i].str, sc[i].len);
		memcpy(concatenated + sc[i].len, delim, delim_len);
		nm_bufferqueue_push(bq, concatenated, sc[i].len + delim_len);
	}

	for (i = 0; sc[i].str; i++) {
		char *ptr = NULL;
		unsigned long len = 0;
		nm_bufferqueue_unshift_to_delim(bq, delim, delim_len, &len, (void **)&ptr);
		t_req(ptr != NULL);
		test(len == sc[i].len + delim_len, "len check, delim '%s' on string %d(%s), expected %ld, was %ld", delim, i, sc[i].str, sc[i].len + delim_len, len);
		test(!memcmp(ptr, sc[i].str, len - delim_len), "memcmp() check, delim '%s' on string %d(%s), expected '%.*s', was '%.*s'", delim, i, sc[i].str, (int) len, sc[i].str, (int) len, ptr - len);
		test(!memcmp(ptr + sc[i].len, delim, delim_len), "memcmp() check, delim '%s' on string %d(%s), expected '%.*s', was '%.*s'", delim, i, sc[i].str, (int) len, sc[i].str, (int) len, ptr - len);
		free(ptr);
	}
	t_end();

	if (delim_len > 1) {
		t_start("Testing delimiter '%s' of len %ld spanning blocks", delim, delim_len);
		for (i = 0; sc[i].str; i++) {
			unsigned int j;
			nm_bufferqueue_push(bq, sc[i].str, sc[i].len);
			for (j = 0; j < delim_len; j++) {
				nm_bufferqueue_push(bq, delim + j, 1);
			}
		}

		for (i = 0; sc[i].str; i++) {
			char *ptr = NULL;
			unsigned long len = 0;
			nm_bufferqueue_unshift_to_delim(bq, delim, delim_len, &len, (void **)&ptr);
			t_req(ptr != NULL);
			test(len == sc[i].len + delim_len, "len check, delim '%s' on string %d(%s), expected %ld, was %ld", delim, i, sc[i].str, sc[i].len + delim_len, len);
			test(!memcmp(ptr, sc[i].str, len - delim_len), "memcmp() check, delim '%s' on string %d(%s), expected '%.*s', was '%.*s'", delim, i, sc[i].str, (int) len, sc[i].str, (int) len, ptr - len);
			test(!memcmp(ptr + sc[i].len, delim, delim_len), "memcmp() check, delim '%s' on string %d(%s), expected '%.*s', was '%.*s'", delim, i, sc[i].str, (int) len, sc[i].str, (int) len, ptr - len);
			free(ptr);
		}
		t_end();
	}

	nm_bufferqueue_destroy(bq);
	return 0;
}

int main(int argc, char **argv)
{
	unsigned int i;
	struct strcode sc[] = {
		ADDSTR("\n"),
		ADDSTR("\0\0"),
		ADDSTR("XXXxXXX"),
		ADDSTR("LALALALALALALAKALASBALLE\n"),
	};

	t_set_colors(0);
	t_start("iocache_use_delim() test");
	for (i = 0; i < ARRAY_SIZE(sc); i++) {
		t_start("Testing delimiter %s of len %ld", sc[i].str, sc[i].len);
		test_delimiter(sc[i].str, sc[i].len);
		t_end();
	}

	return t_end();
}
