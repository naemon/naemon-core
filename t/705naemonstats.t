#!/usr/bin/perl
#
# Checks naemonstats

use warnings;
use strict;
use Test::More;

my $naemonstats = "$ENV{builddir}/src/naemonstats/naemonstats";
my $etc = "$ENV{builddir}/t/etc";
my $var = "$ENV{builddir}/t/var";

plan tests => 10;

my $output = `$naemonstats -c "$etc/naemon-does-not-exist.cfg"`;
isnt( $?, 0, "Bad return code with no config file" );
like( $output, "/Error processing config file/", "No config file" );

$output = `$naemonstats -c "$etc/naemon-no-status.cfg"`;
isnt( $?, 0, "Bad return code with no status file" );
like( $output, "/Error reading status file '.*var/status.dat.no.such.file': No such file or directory/", "No config file" );

$output = `$naemonstats -c "$etc/naemon-no-status.cfg" -m -d NUMHSTUP`;
isnt( $?, 0, "Bad return code with no status file in MRTG mode" );
like( $output, "/^0\$/", "No UP host when no status file" );

$output = `$naemonstats -c "$etc/naemon-with-generated-status.cfg" -m -d NUMHOSTS`;
is( $?, 0, "Bad return code with implied status file in MRTG mode" );
unlike( $output, "/^0\$/", "Implied generated status file contains host(s)" );

$output = `$naemonstats -s "$var/status.dat" -m -d NUMHOSTS`;
is( $?, 0, "Bad return code with explicit status file in MRTG mode" );
unlike( $output, "/^0\$/", "Explicit generated status file contains host(s)" );
