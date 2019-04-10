/*
 * kvvec_ekvstr.h
 *
 *  Created on: Mar 6, 2013
 *      Author: msikstrom
 */

#ifndef KVVEC_EKVSTR_H_
#define KVVEC_EKVSTR_H_

#if !defined (_NAEMON_H_INSIDE) && !defined (NAEMON_COMPILATION)
#error "Only <naemon/naemon.h> can be included directly."
#endif

#include "lnae-utils.h"
#include "kvvec.h"

NAGIOS_BEGIN_DECL

/**
 * This module packs and unpacks kvvecs to strings, to be sent through a data
 * stream.
 *
 * The stream is defined to contain only writable characters, and no newlines.
 *
 * The data is packed as key=value;key=value with ; and = as delimiters.
 *
 * The key an value is escaped as follows:
 *
 * ASCII character 9 (tab) is escaped to \t
 * ASCII character 10 (line feed) is escaped to \n
 * ASCII character 13 (carrage return) is escaped to \r
 * ASCII character 59 (;) is escaped \;
 * ASCII character 61 (=) is escaped \=
 * ASCII character 92 (\) is escaped \\
 * ASCII characters 31 and below, and 127 and above is escaped as \xNN where NN
 *     is the byte in hex
 * all other chars is unescaped
 */

/**
 * Convert a kvvec to a string of format: key=value;key=value, having both key
 * and value escaped, so both key and value is binary safe.
 *
 *
 */
char *kvvec_to_ekvstr( const struct kvvec *kvv );

/**
 * Unpack a string of escaped kvvec to a kvvec
 */
struct kvvec *ekvstr_to_kvvec( const char *inbuf );

NAGIOS_END_DECL

#endif /* KVVEC_EKVSTR_H_ */
