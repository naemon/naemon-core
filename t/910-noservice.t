#!/usr/bin/perl
#
# Check that no service gives the correct message

use warnings;
use strict;
use Test::More;

my $naemon = "$ENV{builddir}/../naemon/naemon";
my $etc = "$ENV{builddir}/etc";
my $precache = "$ENV{builddir}/var/objects.precache";

plan tests => 1;

my $output = `$naemon -v "$etc/naemon-no-service.cfg"`;
like( $output, "/Error: There are no services defined!/", "Correct error for no services" );
