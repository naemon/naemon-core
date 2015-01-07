#!/bin/sh
if test -d .git; then
	git describe --always --tags --dirty | \
		sed -e 's/^v//' -e 's/-[0-9]*-g/-g/' | tr -d '\n'
	exit 0
fi

VERSION=0.9.0
echo -n "${VERSION}-source"
