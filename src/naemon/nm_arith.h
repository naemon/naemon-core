#ifndef NM_ARITH_H
#define NM_ARITH_H
#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <stdbool.h>
#include <limits.h>
#include "lib/lnae-utils.h"

#ifndef NM_SKIP_BUILTIN_OVERFLOW_CHECKS
#  if __GNUC__ >= 5 /* gcc >= 5*/
#	   define NM_HAVE_BUILTIN_OVERFLOW_CHECKS
/* we need to check explicility for clang before doing __has_builtin(), since
 * not doing this causes compilation to fail with 'missing binary operator
 * before token "("' on older gcc's (where the above is not true)
 */
#  elif __CLANG___
#    if __has_builtin(__builtin_smull_overflow) /* clang >= 3.5 */
#	   define NM_HAVE_BUILTIN_OVERFLOW_CHECKS
#    endif
#  endif
#endif
NAGIOS_BEGIN_DECL

/**
 * These functions perform checked arithmetics on their parameters and return
 * the result in the location pointed to by *dest.
 * They return true if no overflow was detected during the operation, and false
 * otherwise
 **/
#ifdef NM_HAVE_BUILTIN_OVERFLOW_CHECKS
static inline bool nm_arith_smull_overflow(long x, long y, long *dest)
{
	return !__builtin_smull_overflow(x, y, dest);
}

static inline bool nm_arith_saddl_overflow(long x, long y, long *dest)
{
	return !__builtin_saddl_overflow(x, y, dest);
}

static inline bool nm_arith_ssubl_overflow(long x, long y, long *dest)
{
	return !__builtin_ssubl_overflow(x, y, dest);
}
#else
static inline bool nm_arith_smull_overflow(long x, long y, long *dest)
{
	if (x > 0) { // x positive
		if (y > 0) {
			if (x > (LONG_MAX / y)) {
				return false;
			}
		} else {
			if (y < (LONG_MIN / x)) {
				return false;
			}
		}
	} else { // x negative or zero
		if (y > 0) {
			if (x < (LONG_MIN / y)) {
				return false;
			}
		} else {
			if ((x != 0) && (y < (LONG_MAX / x))) {
				return false;
			}
		}
	}
	*dest = x * y;
	return true;
}

static inline bool nm_arith_saddl_overflow(long x, long y, long *dest)
{
	if (((y > 0) && (x > (LONG_MAX - y))) ||
		((y < 0) && (x < (LONG_MIN - y)))) {
		return false;
	}
	*dest = x + y;
	return true;
}

static inline bool nm_arith_ssubl_overflow(long x, long y, long *dest)
{
	if ((y > 0 && x < LONG_MIN + y) ||
		(y < 0 && x > LONG_MAX + y)) {
		return false;
	}
	*dest = x - y;
	return true;
}
#endif
NAGIOS_END_DECL
#endif /* NM_ARITH_H */
