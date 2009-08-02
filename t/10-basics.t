#!perl -T

use Test::More 'no_plan';

use threads::lite;

my $queue = threads::lite::queue->new();

$queue->enqueue('foo');

alarm 3;

is($queue->dequeue, 'foo', 'dequeued \'foo\'');

alarm 0;

threads::lite->create(1, 'arguments');

ok(1, 'Created thread');

sleep 1;
