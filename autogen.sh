#!/bin/sh

# running autoreconf twice to workaround a sles 11 clitch: error: possibly undefined macro: AM_PATH_CHECK
autoreconf -s -i || autoreconf -s -i

# not enabling silent rules through configure.ac macros, as I'd like
# non-interactive build outputs to be useful, despite interactive builds
# looking pleasant
./configure --enable-silent-rules "$@"
