#!/bin/sh

VERSION=1.2.0
if test -e .git; then
    if hash git 2>/dev/null; then
        VERSION=$(git describe --tag --exact-match 2>/dev/null | sed -e 's/^v//')
        if [ "x$VERSION" = "x" ]; then
            VERSION=$(git describe --always --tags --dirty | \
                sed -e 's/^v//' -e 's/-[0-9]*-g/-g/' | tr -d '\n' | tr '-' '.').$(date +%Y%m%d)
        fi
    fi
fi

if [ -e .naemon.official ]; then
  echo -n "${VERSION}"
else
  echo -n "${VERSION}.source"
fi

exit 0
