#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT([naemon], [0.8.0], [naemon-dev@monitoring-lists.org])
AC_CONFIG_SRCDIR([naemon/naemon.c])
AM_INIT_AUTOMAKE([subdir-objects])

AC_CONFIG_HEADERS([config.h naemon/lib/snprintf.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_AWK
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
AM_PROG_CC_C_O

AC_ARG_WITH([tests],
            [AS_HELP_STRING([--with-tests], [Enable building tests (requires the check library)])])

AS_IF([test "x$with_tests" != "xno"], [
	m4_ifdef([PKG_CHECK_MODULES],
	         [PKG_CHECK_MODULES([CHECK],
	                            [check >= 0.9.0],
	                            [],
	                            [AC_MSG_RESULT([checking for non-pkgconfig enabled check])])],
	         [true])
	AS_IF([test "x$CHECK_LIBS" = "x"],
	          [m4_ifdef([AM_PATH_CHECK],
	                    [AM_PATH_CHECK([0.9.0])],
	                    [true])])
	AS_IF([test "x$CHECK_LIBS" = "x"],
	      [AS_IF([test "x$with_tests" = "xyes"],
	            [AC_MSG_ERROR([Install check <http://check.sourceforge.net/>, or re-run configure with the --without-tests flag])],
	            [with_tests="no"])],
	      [with_tests="yes"])
])
AM_CONDITIONAL([HAVE_CHECK], [test "x$with_tests" = "xyes"])

AM_MISSING_PROG([PROVE], [prove])
AM_MISSING_PROG([GPERF], [gperf])
# am_missing_prog doesn't seem to fail,
# so I'm putting this redundant check in place to whine more loudly:
AC_CHECK_PROG(GPERF_CHECK,gperf,yes)
if test x"$GPERF_CHECK" != x"yes" ; then
	AC_MSG_ERROR([You need to install gperf <https://www.gnu.org/software/gperf/> before building.])
fi

AM_CPPFLAGS="-D_GNU_SOURCE"
AX_CHECK_COMPILE_FLAG([-Wall], AM_CFLAGS+=" -Wall")
AX_CHECK_COMPILE_FLAG([-Werror], AM_CFLAGS+=" -Werror")
AX_CHECK_COMPILE_FLAG([-fPIC], AM_CFLAGS+=" -fPIC")
AX_CHECK_COMPILE_FLAG([-Wextra], AM_CFLAGS+=" -Wextra")
AX_CHECK_COMPILE_FLAG([-pipe], AM_CFLAGS+=" -pipe")
AX_CHECK_COMPILE_FLAG([-ggdb3], AM_CFLAGS+=" -ggdb3")
AX_CHECK_COMPILE_FLAG([-Wredundant-decls], AM_CFLAGS+=" -Wredundant-decls")
AX_CHECK_COMPILE_FLAG([-Wtrigraphs], AM_CFLAGS+=" -Wtrigraphs")
AX_CHECK_COMPILE_FLAG([-Wdeclaration-after-statement], AM_CFLAGS+=" -Wdeclaration-after-statement")
AX_CHECK_COMPILE_FLAG([-Wshadow], AM_CFLAGS+=" -Wshadow")
AX_CHECK_COMPILE_FLAG([-Wstrict-prototypes], AM_CFLAGS+=" -Wstrict-prototypes")
AX_CHECK_COMPILE_FLAG([-Wno-unused-result], AM_CFLAGS+=" -Wno-unused-result")
AX_CHECK_COMPILE_FLAG([-Wno-unused-parameter], AM_CFLAGS+=" -Wno-unused-parameter")
AX_CHECK_COMPILE_FLAG([-Wno-strict-aliasing], AM_CFLAGS+=" -Wno-strict-aliasing")

AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([ctype.h dirent.h dlfcn.h fcntl.h getopt.h grp.h inttypes.h libgen.h limits.h])
AC_CHECK_HEADERS([locale.h malloc.h memory.h netdb.h netinet/in.h pwd.h regex.h stdarg.h])
AC_CHECK_HEADERS([stdbool.h stdint.h stdlib.h string.h strings.h syslog.h])
AC_CHECK_HEADERS([sys/mman.h sys/resource.h sys/socket.h sys/stat.h sys/time.h])
AC_CHECK_HEADERS([sys/timeb.h sys/types.h sys/wait.h unistd.h vfork.h wchar.h])
AC_CHECK_HEADERS([sys/prctl.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_STRUCT_TIMEZONE
AC_HEADER_STDBOOL

LT_INIT

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
AC_DEFINE(DEFAULT_NAEMON_USER, "naemon", [Default user name to run naemon as])
AC_DEFINE(DEFAULT_NAEMON_GROUP, "naemon", [Default group name to run naemon as])
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
AC_CHECK_FUNCS([dup2 floor ftruncate initgroups munmap pow putenv realpath \
	regcomp select setenv strncasecmp strrchr tzset unsetenv])

# Determine the system init.d directory
AC_ARG_WITH([initdir],
[AS_HELP_STRING([--with-initdir],
[specify the system init script directory @<:@default="${sysconfdir}/init.d"@:>@])],
[], [with_initdir="${sysconfdir}/init.d"])
initrddir="$with_initdir"
AC_SUBST(initrddir)

# Determine the temp directory
AC_ARG_WITH([tempdir],
[AS_HELP_STRING([--with-tempdir],
[specify the temp directory @<:@default="${localstatedir}/cache/naemon"@:>@])],
[], [with_tempdir="${localstatedir}/cache/naemon"])
TMPDIR="$with_tempdir"
AC_SUBST(TMPDIR)

# Determine the check results directory
AC_ARG_WITH([checkresultdir],
[AS_HELP_STRING([--with-checkresultdir],
[specify the checkresults directory @<:@default="${localstatedir}/cache/naemon/checkresults"@:>@])],
[], [with_checkresultdir="${localstatedir}/cache/naemon/checkresults"])
CHECKRESULTDIR="$with_checkresultdir"
AC_SUBST(CHECKRESULTDIR)

# Determine the naemon user
AC_ARG_WITH([naemon-user],
[AS_HELP_STRING([--with-naemon-user],
[specify the naemon user @<:@default="naemon"@:>@])],
[], [with_naemon_user="naemon"])
naemon_user="$with_naemon_user"
AC_SUBST(naemon_user)

# Determine the naemon group
AC_ARG_WITH([naemon-group],
[AS_HELP_STRING([--with-naemon-group],
[specify the naemon group @<:@default="naemon"@:>@])],
[], [with_naemon_group="naemon"])
naemon_grp="$with_naemon_group"
AC_SUBST(naemon_grp)

# Determine the lockfile
AC_ARG_WITH([lockfile],
[AS_HELP_STRING([--with-lockfile],
[specify the naemon lockfile @<:@default="${localstatedir}/cache/naemon/naemon.pid"@:>@])],
[], [with_lockfile="${localstatedir}/cache/naemon/naemon.pid"])
lockfile="$with_lockfile"
AC_SUBST(lockfile)

# Determine the logpath
AC_ARG_WITH([logdir],
[AS_HELP_STRING([--with-logdir],
[specify the naemon log directory @<:@default="${localstatedir}/log/naemon"@:>@])],
[], [with_logdir="${localstatedir}/log/naemon"])
logdir="$with_logdir"
AC_SUBST(logdir)

# Determine the logrotate directory
AC_ARG_WITH([logrotatedir],
[AS_HELP_STRING([--with-logrotatedir],
[specify the log rotate directory])],
[], [with_logrotatedir=""])
logrotatedir="$with_logrotatedir"
AC_SUBST(logrotatedir)

# Determine the naemon plugins dir
AC_ARG_WITH([pluginsdir],
[AS_HELP_STRING([--with-pluginsdir],
[specify the naemon/monitoring plugins directory @<:@default="${libdir}/naemon/plugins"@:>@])],
[], [with_pluginsdir="${libdir}/naemon/plugins"])
pluginsdir="$with_pluginsdir"
AC_SUBST(pluginsdir)

# Determine the mail binary
AC_ARG_WITH([mail],
[AS_HELP_STRING([--with-mail],
[specify the mail binary used for the sample configuration @<:@default="${bindir}/mail"@:>@])],
[], [with_mail="${bindir}/mail"])
MAIL_PROG="$with_mail"
AC_SUBST(MAIL_PROG)

AC_SUBST(libdir)
AC_REQUIRE_AUX_FILE([tap-driver.sh])
AC_CONFIG_FILES([Makefile
                 daemon-init
                 daemon-systemd
                 naemon.logrotate
                 naemon/Makefile
                 naemon/lib/Makefile
                 sample-config/Makefile
                 tests/Makefile
                 t/Makefile
                 t-tap/Makefile
                ])
AC_CONFIG_SUBDIRS([tap])
AC_OUTPUT
