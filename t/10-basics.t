#!perl 

use Test::More 'no_plan';

use threads::lite;

my $thread = threads::lite->spawn({ load  => ['Carp'] }, sub { print STDERR "It seems to be working!\n" } );

ok(1, 'Created thread');

sleep 1;
