#!perl 

use strict;
use warnings;

use Test::More tests => 3;
use Test::Differences;

use threads::lite qw/spawn receive_match/;

my $thread = spawn({ monitor => 1 }, sub { 
	my (undef, $queue) = threads::lite::receive('queue', qr//);
	$queue->enqueue(qw/foo bar baz/);
	$queue->enqueue(qw/1 2 3/);
	});

alarm 5;

my $queue = threads::lite::queue->new;
$thread->send('queue', $queue);

my @first = $queue->dequeue;
my @second = $queue->dequeue;

eq_or_diff \@first, [ qw/foo bar baz/ ], 'first entry is right';
eq_or_diff \@second, [ qw/1 2 3/ ], 'first entry is right';

receive_match {
	when([ 'exit', 'normal', $thread->id ]) {
		eq_or_diff $_, [ 'exit', 'normal', $thread->id], 'thread returned normally';
	};
	when([ 'exit', 'error' ]) {
		ok(0, 'thread returned normally');
	};
};
