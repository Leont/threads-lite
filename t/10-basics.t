#!perl 

use strict;
use warnings;

use Test::More tests => 2;
use Test::Differences;

use threads::lite qw/spawn receive_table/;

my $thread = spawn({ load  => ['Carp'], monitor => 1 }, sub { 42 } );

ok(1, 'Created thread');

alarm 5;

receive_table(
	[ 'exit', 'normal' ] => sub {
		my @arg = @_;
		eq_or_diff \@arg, [ 'exit', 'normal', $thread->id, 42], "Got return value 42";
	},
	[ 'exit', 'error' ]   => sub {
		ok(0, 'Got return value 42');
	},
);
