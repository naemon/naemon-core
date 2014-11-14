#!/bin/sh

autoreconf -s -i
# The silent rules support is enabled in configure.ac, but silent rules isn't
# active by default. This is just a helper for interactive builds, which
# should be easier to track.
./configure --enable-silent-rules "$@"
