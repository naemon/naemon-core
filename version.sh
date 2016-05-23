#!/bin/sh

VERSION=1.0.3
if test -e .git; then
    VERSION=$(git describe --always --tags --dirty | \
        sed -e 's/^v//' -e 's/-[0-9]*-g/-g/' | tr -d '\n')
fi

if [ -e .naemon.official ]; then
  echo -n "${VERSION}-pkg"
else
  echo -n "${VERSION}-source"
fi

exit 0
