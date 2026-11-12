#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "naemon/checks.h"
#include "naemon/shared.h"
#include "naemon/globals.h"
#include "naemon/nm_alloc.h"

static char spool[] = "/tmp/naemon-spool-XXXXXX";
static char elsewhere[] = "/tmp/naemon-cwd-XXXXXX";
static char *old_cwd;

static void setup(void)
{
	ck_assert(mkdtemp(spool) != NULL);
	ck_assert(mkdtemp(elsewhere) != NULL);
	old_cwd = getcwd(NULL, 0);
}

static void teardown(void)
{
	ck_assert(chdir(old_cwd) == 0);
	free(old_cwd);
	rmdir(spool);
	rmdir(elsewhere);
}

static char *path_in(const char *dir, const char *name)
{
	char *p;
	nm_asprintf(&p, "%s/%s", dir, name);
	return p;
}


static void make_old(const char *path)
{
	struct timeval tv[2];
	gettimeofday(&tv[0], NULL);
	tv[0].tv_sec -= 2 * 86400;
	tv[1] = tv[0];
	ck_assert(utimes(path, tv) == 0);
}

static void write_file(const char *path, const char *data)
{
	FILE *fp = fopen(path, "w");
	ck_assert(fp != NULL);
	fputs(data, fp);
	fclose(fp);
}

/* A spool file another program shrinks while naemon reads it used to raise SIGBUS. */
START_TEST(file_shrinking_while_read)
{
	char *f = path_in(spool, "c000001");
	nm_rfile *mf;
	char *line;
	FILE *fp;
	int i;

	fp = fopen(f, "w");
	ck_assert(fp != NULL);
	for (i = 0; i < 100000; i++)
		fprintf(fp, "output=line %d\n", i);
	fclose(fp);

	mf = nm_fopen_ro(f);
	ck_assert(mf != NULL);
	line = nm_fgets(mf);
	ck_assert_str_eq(line, "output=line 0\n");
	free(line);

	ck_assert(truncate(f, 0) == 0);

	for (i = 0; (line = nm_fgets(mf)) != NULL; i++)
		free(line);
	ck_assert_int_lt(i, 100000);
	ck_assert_msg(nm_ferror(mf), "a file that shrank while being read was reported as complete");
	ck_assert_int_eq(ERROR, nm_fclose(mf));
	unlink(f);
	free(f);
}
END_TEST

static int read_all(nm_rfile *mf)
{
	char *line;
	int n = 0;

	while ((line = nm_fgets(mf)) != NULL) {
		free(line);
		n++;
	}
	return n;
}

START_TEST(complete_read_is_no_error)
{
	char *f = path_in(spool, "c000005");
	nm_rfile *mf;

	write_file(f, "a=1\nb=2\nno newline at the end");
	mf = nm_fopen_ro(f);
	ck_assert(mf != NULL);
	ck_assert_int_eq(3, read_all(mf));
	ck_assert_int_eq(3, (int)mf->current_line);
	ck_assert(!nm_ferror(mf));
	ck_assert_int_eq(OK, nm_fclose(mf));
	unlink(f);
	free(f);
}
END_TEST

START_TEST(file_growing_while_read)
{
	char *f = path_in(spool, "c000006");
	nm_rfile *mf;
	FILE *fp;

	write_file(f, "a=1\n");
	mf = nm_fopen_ro(f);
	ck_assert(mf != NULL);
	fp = fopen(f, "a");
	ck_assert(fp != NULL);
	fputs("b=2\n", fp);
	fclose(fp);
	read_all(mf);
	ck_assert_msg(nm_ferror(mf), "a file that grew while being read was reported as complete");
	ck_assert_int_eq(ERROR, nm_fclose(mf));
	unlink(f);
	free(f);
}
END_TEST

START_TEST(read_error_is_reported)
{
	nm_rfile *mf;

	/* a directory opens fine but every read fails with EISDIR */
	mf = nm_fopen_ro(spool);
	ck_assert(mf != NULL);
	ck_assert_int_eq(0, read_all(mf));
	ck_assert(nm_ferror(mf));
	ck_assert_str_eq(nm_ferror_str(mf), strerror(EISDIR));
	ck_assert_int_eq(ERROR, nm_fclose(mf));
}
END_TEST

/* Expired files must be removed from the spool directory, not the working directory. */
START_TEST(expired_file_removed_from_spool)
{
	char *f = path_in(spool, "c000002"), *ok = path_in(spool, "c000002.ok");
	char *decoy = path_in(elsewhere, "c000002"), *decoy_ok = path_in(elsewhere, "c000002.ok");

	write_file(f, "file_time=1\n");
	write_file(ok, "");
	make_old(f);
	write_file(decoy, "");
	write_file(decoy_ok, "");
	ck_assert(chdir(elsewhere) == 0);

	max_check_result_file_age = 3600;
	max_check_reaper_time = 30;
	process_check_result_queue(spool);

	ck_assert_msg(access(f, F_OK) != 0, "expired check result file was not removed");
	ck_assert_msg(access(ok, F_OK) != 0, "expired ok-to-go file was not removed");
	ck_assert_msg(access(decoy, F_OK) == 0, "a file in the working directory was removed");
	ck_assert_msg(access(decoy_ok, F_OK) == 0, "a file in the working directory was removed");

	unlink(decoy);
	unlink(decoy_ok);
	free(f);
	free(ok);
	free(decoy);
	free(decoy_ok);
}
END_TEST

/* A result file that cannot be opened is dropped together with its ok-to-go file. */
START_TEST(unreadable_file_removed_with_ok_file)
{
	char *f = path_in(spool, "c000003"), *ok = path_in(spool, "c000003.ok");

	/* a dangling symlink cannot be opened, not even by root */
	ck_assert(symlink("/nonexistent/naemon-spool-test", f) == 0);
	write_file(ok, "");

	ck_assert_int_eq(ERROR, process_check_result_file(f));
	ck_assert_msg(access(ok, F_OK) != 0, "ok-to-go file was left behind");

	free(f);
	free(ok);
}
END_TEST

/* A result file that cannot be read completely is kept for the next run. */
START_TEST(incomplete_result_file_is_kept)
{
	char *f = path_in(spool, "c000007"), *ok = path_in(spool, "c000007.ok");

	/* a directory under a result file name: opens, but cannot be read */
	ck_assert(mkdir(f, 0700) == 0);
	write_file(ok, "");

	ck_assert_int_eq(ERROR, process_check_result_file(f));
	ck_assert_msg(access(f, F_OK) == 0, "incompletely read result file was removed");
	ck_assert_msg(access(ok, F_OK) == 0, "ok-to-go file of an incompletely read result file was removed");

	unlink(ok);
	rmdir(f);
	free(f);
	free(ok);
}
END_TEST

int main(void)
{
	int failed;
	Suite *s = suite_create("Check result spool");
	TCase *tc = tcase_create("spool");
	SRunner *sr;

	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_add_test(tc, file_shrinking_while_read);
	tcase_add_test(tc, expired_file_removed_from_spool);
	tcase_add_test(tc, unreadable_file_removed_with_ok_file);
	tcase_add_test(tc, complete_read_is_no_error);
	tcase_add_test(tc, file_growing_while_read);
	tcase_add_test(tc, read_error_is_reported);
	tcase_add_test(tc, incomplete_result_file_is_kept);
	suite_add_tcase(s, tc);

	sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
