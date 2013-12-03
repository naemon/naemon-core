#!/bin/sh

for f in `find ./naemon ./t-tap -type f -name "*.[c]"`; do
	./indent.sh $f
done

