#!perl -T

use Test::More 'no_plan';

use threads::lite;

my $thread = threads::lite->create( [ _start => sub { print STDERR "It seems to be working!\n" } ] );
#my $thread = threads::lite->create(1, 'arguments', [1, 2, 3]);

ok(1, 'Created thread');

sleep 1;
