#!/bin/sh

# Usage: indent-all.sh [--check]

set -e

ARTISTIC_STYLE_OPTIONS=/dev/null
export ARTISTIC_STYLE_OPTIONS
tmpfile=''

cleanup() {
	test "$tmpfile" != '' && rm -f "$tmpfile"
}
trap cleanup EXIT

astyle_options="--style=linux \
	--indent=tab \
	--unpad-paren \
	--pad-oper \
	--pad-header \
	--lineend=linux \
	--align-pointer=name \
	--keep-one-line-statements \
	--preserve-date \
	--formatted \
	--recursive \
	--errors-to-stdout"

astyle_paths='lib/\*.c src/\*.c t-tap/\*.c tests/\*.c'

if ! hash astyle 2> /dev/null; then
	echo "ERROR: 'astyle' is not installed on the system, cannot continue indenting..." >&2
	exit 1
fi

if [ "${1:-}" = "--check" ]; then
	tmpfile=$(mktemp)
	astyle $astyle_options --dry-run $astyle_paths | tee "$tmpfile"
	if grep -q Formatted "$tmpfile"; then
		echo "ERROR: astyle regressions found. Run '$0' to fix." >&2
		exit 1
	fi
	exit 0
fi

astyle $astyle_options --suffix=.pre-indent $astyle_paths
