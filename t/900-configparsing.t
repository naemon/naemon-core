#!/usr/bin/perl
#
# Taking a known naemon configuration directory, will check that the objects.cache is as expected

use warnings;
use strict;
use Test::More;

my $buildroot=".";
if (defined $ENV{builddir}) {
	$buildroot = $ENV{builddir};
}

my $naemon = "$buildroot/src/naemon/naemon";
my $etc = "$buildroot/t/etc";
my $precache = "$buildroot/t/var/objects.precache";

my $options = $> == 0 ? '--allow-root' : '';
plan tests => 12;

my $output = `$naemon $options -v "$etc/naemon.cfg"`;
if ($? == 0) {
	pass("Naemon validated test configuration successfully");
} else {
	fail("Naemon validation failed:\n$output");
}

system("$naemon $options -vp '$etc/naemon.cfg'");
is($?, 0, "Cannot create precached objects file");
system("grep -v 'Created:' $precache > '$precache.generated'");

my $diff = "diff -u $precache.expected $precache.generated";
my @output = `$diff`;
if ($? == 0) {
	pass( "Naemon precached objects file matches expected" );
} else {
	fail( "Naemon precached objects discrepency!!!\nTest with: $diff\nCopy with: cp $precache.generated $precache.expected" );
	print "#$_" foreach @output;
}

# naemon-service-dependencies.cfg
$precache = "$buildroot/t/var/objects.precache.naemon-service-dependencies";
system("$naemon $options -vp '$etc/naemon-service-dependencies.cfg'");
is($?, 0, "Cannot create precached objects file");
system("grep -v 'Created:' $precache > '$precache.generated'");

$diff = "diff -u $precache.expected $precache.generated";
@output = `$diff`;
if ($? == 0) {
	pass( "Naemon precached objects file matches expected" );
} else {
	fail( "Naemon precached objects discrepency!!!\nTest with: $diff\nCopy with: cp $precache.generated $precache.expected" );
	print "#$_" foreach @output;
}


my $out = `$naemon $options -v '$etc/naemon-duplicate-service-warning.cfg' 2>&1`;
my $rc  = $?>>8;
is($rc, 0);
like($out, "/Duplicate definition found for service/", "output contains warning");


$out = `$naemon $options -v '$etc/naemon-duplicate-host-error.cfg' 2>&1`;
$rc = ($?>> 8) & 0xff;
is($rc, 1);
like($out, "/Duplicate definition found for host/", "output contains error");

$out = `$naemon $options -v '$etc/naemon-missing-service-description-error.cfg' 2>&1`;
$rc  = $?>>8 & 0xff;
is($rc, 1);
like($out, "/Service has no hosts and/or service_description/", "output contains error");

$out = `$naemon $options -v "$etc/no-objects.cfg"`;
is($?, 0, "Naemon does not need any objects to start");
