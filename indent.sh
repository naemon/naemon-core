#!/bin/sh
ARTISTIC_STYLE_OPTIONS=/dev/null

export ARTISTIC_STYLE_OPTIONS

astyle --style=linux \
	--indent=tab \
	--unpad-paren --pad-oper --pad-header \
	--lineend=linux \
	--align-pointer=name --keep-one-line-statements \
	--formatted --preserve-date \
	--suffix=.pre-indent "$@"
