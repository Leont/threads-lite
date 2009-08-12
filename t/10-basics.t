#!perl 

use Test::More 'no_plan';
use Test::Differences;

use threads::lite;

my $thread = threads::lite->spawn({ load  => ['Carp'], monitor => 1 }, sub { print STDERR "It seems to be working!\n"; 42 } );

ok(1, 'Created thread');

alarm 5;

receive_table(
	[ 'normal' ] => sub {
		my @arg = @_;
		eq_or_diff \@arg, [ 'normal', 42], "Got return value 42";
	},
	[ 'exit' ]   => sub {
		ok(0, "Got return value 42");
	}
);
