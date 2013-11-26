#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([naemon], [4.0-beta5], [naemon-dev@monitoring-lists.org])
AC_CONFIG_SRCDIR([naemon/naemon.c])
AM_INIT_AUTOMAKE

AC_CONFIG_HEADERS([config.h naemon/lib/snprintf.h])
AC_PREFIX_DEFAULT(/usr/local/naemon)

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AM_PROG_CC_C_O
AM_MISSING_PROG([GPERF], [gperf])

# Add some crap here so we can check which compiler flags are supported
CFLAGS="${CFLAGS} -Wall -Wno-unused-result -fPIC -D_GNU_SOURCE"
#CC_ADD_FLAG([-fPIC])
#CC_ADD_FLAG([-ggdb3])
#CC_ADD_FLAG([-pipe])
#CC_ADD_FLAG([-Wall])
#CC_ADD_FLAG([-Wextra])
#CC_ADD_FLAG([-Wredundant-decls])
#CC_ADD_FLAG([-Wtrigraphs])
#CC_ADD_FLAG([-Wdeclaration-after-statement])
#CC_ADD_FLAG([-Wshadow])
#CC_ADD_FLAG([-Wstrict-prototypes])
#CC_ADD_FLAG([-Wno-unused-result])
#CC_ADD_FLAG([-Wno-unused-parameter])


# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h ctype.h dirent.h dlfcn.h fcntl.h getopt.h grp.h inttypes.h libgen.h limits.h])
AC_CHECK_HEADERS([locale.h malloc.h memory.h netdb.h netinet/in.h pwd.h regex.h stdarg.h])
AC_CHECK_HEADERS([stdbool.h stdint.h stdlib.h string.h strings.h syslog.h])
AC_CHECK_HEADERS([sys/mman.h sys/resource.h sys/socket.h sys/stat.h sys/time.h])
AC_CHECK_HEADERS([sys/timeb.h sys/types.h sys/wait.h unistd.h vfork.h wchar.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_STRUCT_TIMEZONE
AC_HEADER_STDBOOL


dnl Check for asprintf() and friends...
AC_CACHE_CHECK([for va_copy],ac_cv_HAVE_VA_COPY,[
AC_TRY_LINK([#include <stdarg.h>
va_list ap1,ap2;], [va_copy(ap1,ap2);],
ac_cv_HAVE_VA_COPY=yes,
ac_cv_HAVE_VA_COPY=no)])
if test x"$ac_cv_HAVE_VA_COPY" = x"yes"; then
    AC_DEFINE(HAVE_VA_COPY,1,[Whether va_copy() is available])
else
    AC_CACHE_CHECK([for __va_copy],ac_cv_HAVE___VA_COPY,[
    AC_TRY_LINK([#include <stdarg.h>
    va_list ap1,ap2;], [__va_copy(ap1,ap2);],
    ac_cv_HAVE___VA_COPY=yes,
    ac_cv_HAVE___VA_COPY=no)])
    if test x"$ac_cv_HAVE___VA_COPY" = x"yes"; then
        AC_DEFINE(HAVE___VA_COPY,1,[Whether __va_copy() is available])
    fi
fi

AC_DEFINE(NAEMON_PROCESS_NAME, "Naemon Process", [Process name for naemon])
AC_DEFINE(DEFAULT_NAGIOS_USER, "naemon", [Default user name to run naemon as])
AC_DEFINE(DEFAULT_NAGIOS_GROUP, "naemon", [Default group name to run naemon as])
AC_DEFINE(USE_EVENT_BROKER, 1, [Use event brokers])

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_FUNC_MMAP
AC_FUNC_WAIT3

# We expect full C89 and almost-full POSIX.1-2001 compliance where
# we're compiled. That covers all linuxes, bsd's and solaris boxen
# in use today, and saves us the hassle of bothering about Irix,
# AIX and other really ancient crap systems.
# Don't add functions to this list that are in either C89 or in
# both POSIX.1-2001 and C99. Just let the compile fail instead.
# Also, obviously, don't add functions that aren't used in the code.
AC_CHECK_FUNCS([dup2 floor ftruncate munmap pow putenv realpath \
	regcomp select setenv strncasecmp strrchr tzset])

AC_CONFIG_FILES([Makefile
                 naemon/Makefile
                 naemon/lib/Makefile
                 ])
#AC_CONFIG_SUBDIRS([])
AC_OUTPUT
