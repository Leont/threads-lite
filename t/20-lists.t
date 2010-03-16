#!perl 

use Test::More 'no_plan';
use Test::Differences;

use threads::lite::list 'paralel_map';

alarm 5;

my @reference = map { $_ * 2} 1 .. 4;
{
	my @foo = paralel_map { $_ * 2 } 1..4;

	eq_or_diff(\@foo, \@reference, "tmap { \$_ * 2 } 1..4");
}

