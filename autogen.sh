#!/bin/sh

autoreconf -s -i
# not enabling silent rules through configure.ac macros, as I'd like
# non-interactive build outputs to be useful, despite interactive builds
# looking pleasant
./configure --enable-silent-rules "$@"
