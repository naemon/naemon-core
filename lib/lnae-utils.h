#ifndef LIBNAEMON_lnae_utils_h__
#define LIBNAEMON_lnae_utils_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <unistd.h> /* for sysconf() */
#include <stdlib.h> /* for rand() */

/**
 * @file lnae-utils.h
 * @brief helper and compatibility macros that lack a "real" home.
 *
 * This is the home of random macros that must be present for compilation
 * to succeed but are missing on some platforms.
 *
 * @{
 */

#define NAEMON_MKVERSION(a, b, c) \
	(((a) * 10000) + ((b) * 100) + (c))

#ifdef __cplusplus
/** C++ compatibility macro that avoids confusing indentation programs */
# define NAEMON_BEGIN_DECL extern "C" {
/**
 * Use at end of header file declarations to obtain C++ compatibility
 * ... without confusing indentation programs
 */
# define NAEMON_END_DECL }
#else
/** C++ compatibility macro that avoids confusing indentation programs */
# define NAEMON_BEGIN_DECL /* nothing */
/** C++ compatibility macro that avoid confusing indentation programs */
# define NAEMON_END_DECL /* more of nothing */
#endif

/** Compat macro */
#define NAGIOS_BEGIN_DECL NAEMON_BEGIN_DECL
/** Compat macro */
#define NAGIOS_END_DECL NAEMON_END_DECL

#ifndef NODOXY /* doxy comments are useless here */
# ifndef __GNUC__
#  define GCC_VERSION 0
#  define __attribute__(x) /* nothing */
# else
#  ifdef __GNUC_PATCHLEVEL__
#   define GCC_VERSION NAEMON_MKVERSION(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#  else
#   define GCC_VERSION NAEMON_MKVERSION(__GNUC__, __GNUC_MINOR__, 0)
#  endif /* __GNUC_PATCHLEVEL__ */
# endif /* __GNUC__ */
#endif /* NODOXY */

#if GCC_VERSION >= NAEMON_MKVERSION(4, 5, 0)
# define NAEMON_DEPRECATED(version, hint) \
	__attribute__((deprecated("This function will be removed in version " #version ". Please use " #hint " instead")))
#else
/** Macro for alerting module authors to function deprecation */
# define NAEMON_DEPRECATED(version, hint) \
	__attribute__((deprecated))
#endif

/*
 * These macros are widely used throughout Nagios
 */
#define	OK       0   /**< Indicates successful function call in Nagios */
#define ERROR   -2   /**< Non-successful function call in Nagios */

#ifdef FALSE
#undef FALSE
#endif
#define FALSE 0 /**< Not true */

#ifdef TRUE
#undef TRUE
#endif
#define TRUE (!FALSE) /**< Not false */

#ifndef ARRAY_SIZE
/** Useful for iterating over all elements in a static array */
# define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif
#ifndef veclen
/** useful for iterating over all elements in a static array */
# define veclen ARRAY_SIZE
#endif

#ifndef offsetof
/** standard offsetof macro */
# define offsetof(t, f) ((unsigned long)&((t *)0)->f)
#endif

/** character map initialization for .bss-allocated char maps */
#define CHAR_MAP_INIT(k) { \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, k, \
	}

/** @} */
#endif
