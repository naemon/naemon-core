#ifndef LIBNAEMON_runcmd_h__
#define LIBNAEMON_runcmd_h__

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include <signal.h>
#include "lnae-utils.h"

/**
 * @file runcmd.h
 * @brief runcmd library function declarations
 *
 * @note This is inherited from the nagiosplugins project, although
 * I (AE) wrote the original code, and it might need refactoring
 * for performance later.
 * @{
 */

/** Return code bitflags for runcmd_cmd2strv() */
#define RUNCMD_HAS_REDIR (1 << 0) /**< I/O redirection */
#define RUNCMD_HAS_SUBCOMMAND  (1 << 1) /**< subcommands present */
#define RUNCMD_HAS_PAREN (1 << 2) /**< parentheses present in command */
#define RUNCMD_HAS_JOBCONTROL (1 << 3) /**< job control stuff present */
#define RUNCMD_HAS_UBSQ (1 << 4) /**< unbalanced single quotes */
#define RUNCMD_HAS_UBDQ (1 << 5) /**< unbalanced double quotes */
#define RUNCMD_HAS_WILDCARD (1 << 6) /**< wildcards present */
#define RUNCMD_HAS_SHVAR    (1 << 7) /**< shell variables present */


#define RUNCMD_EFD    (-1)  /**< Failed to pipe() or open() */
#define RUNCMD_EALLOC (-2)  /**< Failed to alloc */
#define RUNCMD_ECMD   (-3)  /**< Bad command */
#define RUNCMD_EFORK  (-4)  /**< Failed to fork() */
#define RUNCMD_EINVAL (-5)  /**< Invalid parameters */
#define RUNCMD_EWAIT  (-6)  /**< Failed to wait() */

NAGIOS_BEGIN_DECL

/**
 * Initialize the runcmd library.
 *
 * Only multi-threaded programs that might launch the first external
 * program from multiple threads simultaneously need to bother with
 * this.
 */
extern void runcmd_init(void);

/**
 * Return pid of a command with a specific file descriptor
 * @param[in] fd stdout filedescriptor of the child to get pid from
 * @return pid of the child, or 0 on errors
 */
extern pid_t runcmd_pid(int fd);

/**
 * Return explanation of which system call or operation failed
 * @param code Error code returned by a library function
 * @return A non-free()'able string explaining where the error occurred
 */
extern const char *runcmd_strerror(int code);

/**
 * Start a command from a command string
 * @param[in] cmdstring The command to launch
 * @param[out] pfd Child's stdout filedescriptor
 * @param[out] pfderr Child's stderr filedescriptor
 */
extern int runcmd_open(const char *cmdstring, int *pfd, int *pfderr)
	__attribute__((__nonnull__(1, 2, 3)));

/**
 * Close a command and return its exit status
 * @note Don't use this. It's a retarded way to reap children suitable
 * only for launching a one-shot program.
 *
 * @param[in] fd The child's stdout filedescriptor
 * @return exit-status of the child, or -1 in case of errors
 */
extern int runcmd_close(int fd);

/**
 * Convert a string to a vector of arguments like a shell would
 * @note This might have bugs and is only tested to behave similar
 * to how /bin/sh does things. For csh or other non bash-ish shells
 * there are no guarantees.
 * @note The out_argv array has to be large enough to hold all strings
 * found in the command.
 * @param[in] str The string to convert to an argument vector
 * @param[out] out_argc The number of arguments found
 * @param[out] out_argv The argument vector
 * @return 0 on (great) success, or a bitmask of failure-codes
 * representing f.e. unclosed quotes, job control or output redirection.
 * See the RUNCMD_HAS_* and their ilk to find out about the flag.
 */
extern int runcmd_cmd2strv(const char *str, int *out_argc, char **out_argv, int *out_envc, char **out_env);

NAGIOS_END_DECL

#endif /* INCLUDE_runcmd_h__ */
/** @} */
