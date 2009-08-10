#!perl -T

use Test::More 'no_plan';

use threads::lite;

my $thread = threads::lite->spawn( [ _start => sub { print STDERR "It seems to be working!\n" } ] );

ok(1, 'Created thread');

sleep 1;
