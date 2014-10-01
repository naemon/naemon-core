#!/usr/bin/perl
#
# Check that no contactgroup gives the correct message

use warnings;
use strict;
use Test::More qw(no_plan);

my $naemon = "$ENV{builddir}/../naemon/naemon";
my $etc = "$ENV{builddir}/etc";
my $precache = "$ENV{builddir}/var/objects.precache";


my $output = `$naemon -v "$etc/naemon-no-contactgroup.cfg"`;
like( $output, "/Error: Could not find any contactgroup matching 'nonexistantone'/", "Correct error for no contactgroup" );
isnt($?, 0, "And get return code error" );
