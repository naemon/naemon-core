#!/usr/bin/perl
#
# Taking a known naemon configuration directory, will check that the objects.cache is as expected

use warnings;
use strict;
use Test::More;

my $naemon = "$ENV{builddir}/../naemon/naemon";
my $etc = "$ENV{builddir}/etc";
my $precache = "$ENV{builddir}/var/objects.precache";

plan tests => 2;

my $output = `$naemon -v "$etc/naemon.cfg"`;
if ($? == 0) {
	pass("Naemon validated test configuration successfully");
} else {
	fail("Naemon validation failed:\n$output");
}

system("$naemon -vp '$etc/naemon.cfg' > /dev/null") == 0 or die "Cannot create precached objects file";
system("grep -v 'Created:' $precache > '$precache.generated'");

my $diff = "diff -u $precache.expected $precache.generated";
my @output = `$diff`;
if ($? == 0) {
	pass( "Naemon precached objects file matches expected" );
} else {
	fail( "Naemon precached objects discrepency!!!\nTest with: $diff\nCopy with: cp $precache.generated $precache.expected" );
	print "#$_" foreach @output;
}	

