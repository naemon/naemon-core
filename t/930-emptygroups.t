#!/usr/bin/perl
#
# Check that empty host/service groups pass verfication.
# Likely error on non-patched version:
# "Error: Host 'r' specified in host group 'generic-pc' is not defined anywhere!"

use warnings;
use strict;
use Test::More;

my $naemon = "$ENV{builddir}/../naemon/naemon";
my $etc = "$ENV{builddir}/etc";

plan tests => 1;

my @output = `$naemon -v "$etc/naemon-empty-groups.cfg"`;
if ($? == 0) {
	pass("Naemon validated empty host/service-group successfully");
} else {
	@output = grep(/^Error: .+$/g, @output);
	fail("Naemon validation failed:\n@output");
}
