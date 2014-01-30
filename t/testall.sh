#!/bin/sh

dir=$(dirname $0)

if [ -n "$builddir" ]; then
	cp -R $dir/etc "$builddir";
	cp -R $dir/var "$builddir";
	chmod -R u+w $builddir/var $builddir/etc
else
	builddir=$dir
	export builddir
fi
prove $dir/705nagiostats.t $dir/900-configparsing.t $dir/910-noservice.t $dir/920-nocontactgroup.t $dir/930-emptygroups.t
result=$?

if [ "$builddir" != "$dir" ]; then
	rm -r "$builddir/etc";
	rm -r "$builddir/var";
fi

exit $result
