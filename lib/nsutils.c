#include "lnae-utils.h"
#include "nsutils.h"
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

#if defined(hpux) || defined(__hpux) || defined(_hpux)
#  include <sys/pstat.h>
#endif

/*
 * By doing this in two steps we can at least get
 * the function to be somewhat coherent, even
 * with this disgusting nest of #ifdefs.
 */
#ifndef _SC_NPROCESSORS_ONLN
#  ifdef _SC_NPROC_ONLN
#    define _SC_NPROCESSORS_ONLN _SC_NPROC_ONLN
#  elif defined _SC_CRAY_NCPU
#    define _SC_NPROCESSORS_ONLN _SC_CRAY_NCPU
#  endif
#endif

int real_online_cpus(void)
{
#ifdef _SC_NPROCESSORS_ONLN
	long ncpus;

	if ((ncpus = (long)sysconf(_SC_NPROCESSORS_ONLN)) > 0)
		return (int)ncpus;
#elif defined(hpux) || defined(__hpux) || defined(_hpux)
	struct pst_dynamic psd;

	if (!pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0))
		return (int)psd.psd_proc_cnt;
#endif

	return 0;
}

int online_cpus(void)
{
	int ncpus = real_online_cpus();
	return ncpus > 0 ? ncpus : 1;
}

int tv_delta_msec(const struct timeval *start, const struct timeval *stop)
{
	return (stop->tv_sec - start->tv_sec) * 1000 + (stop->tv_usec - start->tv_usec) / 1000;
}

float tv_delta_f(const struct timeval *start, const struct timeval *stop)
{
#define DIVIDER 1000000
	float ret;
	time_t usecs, stop_usec;

	ret = stop->tv_sec - start->tv_sec;
	stop_usec = stop->tv_usec;
	if (stop_usec < start->tv_usec) {
		ret -= 1.0;
		stop_usec += DIVIDER;
	}
	usecs = stop_usec - start->tv_usec;

	ret += (float)((float)usecs / DIVIDER);
	return ret;
}

#define MKSTR_BUFS 256 /* should be plenty */
const char *mkstr(const char *fmt, ...)
{
	static char buf[MKSTR_BUFS][32]; /* 8k statically on the stack */
	static int slot = 0;
	char *ret;

	va_list ap;
	va_start(ap, fmt);
	ret = buf[slot++ % MKSTR_BUFS];
	vsnprintf(ret, sizeof(buf[0]), fmt, ap);
	va_end(ap);
	return ret;
}

/* format duration seconds into human readable string */
const char* duration_string(unsigned long duration) {
	int days, hours, minutes, seconds;

	days = duration / 86400;
	duration -= (days * 86400);
	hours = duration / 3600;
	duration -= (hours * 3600);
	minutes = duration / 60;
	duration -= (minutes * 60);
	seconds = duration;
	return (char *)mkstr("%dd %dh %dm %ds", days, hours, minutes, seconds);
}

/* close and reopen stdin, stdout and stderr to /dev/null */
void close_standard_fds(void)
{
	/* close existing stdin, stdout, stderr */
	close(0);
	close(1);
	close(2);

	/* THIS HAS TO BE DONE TO AVOID PROBLEMS WITH STDERR BEING REDIRECTED TO SERVICE MESSAGE PIPE! */
	/* re-open stdin, stdout, stderr with known values */
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_WRONLY);

	return;
}