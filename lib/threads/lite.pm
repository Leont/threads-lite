package threads::lite;

use strict;
use warnings;

our $VERSION = '0.023';

use 5.010;

use base qw/Exporter/;
use Storable 2.05 ();
use Data::Dumper;

use XSLoader;
XSLoader::load('threads::lite', $VERSION);

our @EXPORT_OK = qw/spawn receive receive_nb receive_table receive_table_nb self/;

require threads::lite::tid;

sub _receive;
sub _receive_nb;
sub self;

my @message_cache;

sub _deep_equals {
	my ($message, $criterion) = @_;
	return if $#{$message} < $#{$criterion};
	for my $i (0..$#{$criterion}) {
		return if not $message->[$i] ~~ $criterion->[$i];
	}
	return 1;
}

sub _return_elements {
	my @args = @_;
	return wantarray ? @args : $args[0];
}

sub _match_mailbox {
	my ($criterion) = @_;
	for my $i (0..$#message_cache) {
		next if not _deep_equals($message_cache[$i], $criterion);
		return @{ splice @message_cache, $i, 1 };
	}
	return;
}

##no critic (Subroutines::RequireFinalReturn)

sub receive {
	my @args = @_;
	if (my @ret = _match_mailbox(\@args)) {
		return @ret;
	}
	while (1) {
		my @next = _receive;
		return _return_elements(@next) if _deep_equals(\@next, \@args);
		push @message_cache, \@next;
	}
}

sub receive_nb {
	my @args = @_;
	if (my @ret = _match_mailbox(\@args)) {
		return @ret;
	}
	while (my @next = _receive_nb) {
		return _return_elements(@next) if _deep_equals(\@next, \@args);
		push @message_cache, \@next;
	}
	return;
}

sub receive_table {
	my @args = @_;
	my @table;

	push @table, [ splice @args, 0, 2 ] while @args >= 2;

	for my $pair (@table) {
		if (my @ret = _match_mailbox($pair->[0])) {
			$pair->[1]->(@ret) if defined $pair->[1];
			return _return_elements(@ret);
		}
	}
	while (1) {
		my @next = _receive;
		for my $pair (@table) {
			if (_deep_equals(\@next, $pair->[0])) {
				$pair->[1]->(@next) if defined $pair->[1];
				return _return_elements(@next);
			}
		}
		push @message_cache, \@next;
	}
}

sub receive_table_nb {
	my @args = @_;
	my @table;

	push @table, [ splice @args, 0, 2 ] while @args >= 2;

	for my $pair (@table) {
		if (my @ret = _match_mailbox($pair->[0])) {
			$pair->[1]->(@ret) if defined $pair->[1];
			return _return_elements(@ret);
		}
	}
	while (my @next = _receive) {
		for my $pair (@table) {
			if (_deep_equals(\@next, $pair->[0])) {
				$pair->[1]->(@next) if defined $pair->[1];
				return _return_elements(@next);
			}
		}
		push @message_cache, \@next;
	}
	return;
}

1;

__END__

=head1 NAME

threads::lite - Yet another threads library

=head1 VERSION

Version 0.023

=head1 SYNOPSIS

 use Modern::Perl;
 use threads::lite qw/spawn self receive receive_table/;

 sub child {
     my $other = threads::lite::receive;
     while (<>) {
         chomp;
         $other->send(line => $_);
     }
 }

 my $child = spawn({ monitor => 1 } , \&child);
 $child->send(self);

 my $continue = 1;
 while ($continue) {
	 receive_table(
		 [ 'line' ] => sub {
			 my (undef, $line) = @_;
			 say "received line: $line";
		 },
		 [ 'exit', qr//, $child->id ] => sub {
			 say "received end of file";
			 $continue = 0;
		 },
		 [] => sub {
			 die sprintf "Got unknown message: (%s)", join ", ", @_;
		 },
	 );
 };

=head1 DESCRIPTION

This module implements threads for perl. One crucial difference with normal threads is that the threads are disconnected, except by message queues (channel). It thus facilitates a message passing style of multi-threading.

=head1 FUNCTIONS

All these functions are exported optionally.

=head2 Utility functions

=head3 spawn($options, $sub)

Spawn new threads. It will run $sub and send all monitoring processes it's return value. $options is a hashref that can contain the following elements.

=over 2

=item * modules => [...]

Load the specified modules before running any code.

=item * pool_size => int

Create C<pool_size> identical clones. The threads are cloned right after module load time, but before any code is run.

=item * monitor => 0/1

If this is true, the calling process will monitor the newly spawned threads. Defaults to false.

=item * stack_size => int

The stack size for the newly created threads. It defaults to 64 kiB.

=back

$sub can be a function name or a subref. If it is a name, you must make sure the module it is in is loaded in the new thread. If it is a reference it will be serialized and sent to the new thread. This means that any enclosed variables will probability not work as expected.

=head3 self

Retreive the tid corresponding with the current thread.

=head2 Receiving functions

threads::lite defines four functions for receiving messages from the thread's mailbox. Each of them accepts matching patterns used to select those messages. Pattern matching works like this:

=over 2

=item * If the pattern contains more elements than the message, the match fails.

=item * If the pattern contains more elements than the message, the superfluous elements are ignored for the match.

=item * Each of the elements in the message is smartmatched against the corresponding element in the pattern. Smartmatching semantics are defined in L<perlsyn|perlsyn/<"Smart-matching-in-detail">. If an element fails, the whole match fails.

=item * When a match fails, the next message on the queue is tried.

=back

=head3 receive(@pattern)

Return the first message matching pattern @pattern. If there is no such message in the queue, it blocks until a suitable message is received.

=head3 receive_nb(@pattern)

Return the first message matching pattern @pattern. If there is no such message in the queue, it returns an empty list (undef in scalar context).

=head3 receive_table( [@pattern] => action...)

This goes through each of the patterns until it can find one that matches a message on the queue, and call its action if it is defined. If none of the patterns match any of the messages in the queue, it blocks until a suitable message is received.

=head3 receive_table_nb( [@pattern] => action...)

This goes through each of the patterns until it can find one that matches a message on the queue, and call its action if it is defined. If none of the patterns match any of the messages in the queue, it blocks until a suitable message is received. If none of the patterns match any of the messages in the queue, it return an empty list.

=head1 AUTHOR

Leon Timmermans, C<< <leont at cpan.org> >>

=head1 BUGS

This is an early development release, and is expected to be buggy and incomplete.

Please report any bugs or feature requests to C<bug-threads-lite at rt.cpan.org>, or through
the web interface at L<http://rt.cpan.org/NoAuth/ReportBug.html?Queue=threads-lite>.  I will be notified, and then you'll
automatically be notified of progress on your bug as I make changes.


=head1 SUPPORT

You can find documentation for this module with the perldoc command.

    perldoc threads::lite


You can also look for information at:

=over 4

=item * RT: CPAN's request tracker

L<http://rt.cpan.org/NoAuth/Bugs.html?Dist=threads-lite>

=item * AnnoCPAN: Annotated CPAN documentation

L<http://annocpan.org/dist/threads-lite>

=item * CPAN Ratings

L<http://cpanratings.perl.org/d/threads-lite>

=item * Search CPAN

L<http://search.cpan.org/dist/threads-lite>

=back

=head1 COPYRIGHT & LICENSE

Copyright 2009 Leon Timmermans, all rights reserved.

This program is free software; you can redistribute it and/or modify it
under the same terms as Perl itself.

=cut
