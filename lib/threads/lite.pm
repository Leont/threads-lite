package threads::lite;

use strict;
use warnings;

our $VERSION = '0.010_001';

use 5.010;

use base qw/DynaLoader Exporter/;
use Storable 2.05 ();

threads::lite->bootstrap($VERSION);

##no critic ProhibitAutomaticExportation
our @EXPORT = qw/receive receive_nb receive_table self/;

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

sub _get_runtime {
	my $ret;
	1 while (
		receive_table(
			['load'] => \&_load_module,
			['run']  => sub { $ret = $_[1] },
		) ne 'run'
	);
	return $ret;
}

sub spawn {
	my ($class, $options, $args) = @_;
	my $thread = $class->_create($options->{monitor});
	for my $module (@{ $options->{modules} }) {
		$thread->send(load => $module);
	}
	$thread->send(run => $args);
	return $thread;
}

##no critic Subroutines::RequireFinalReturn

sub receive {
	my @args = @_;
	if (my @ret = _match_mailbox(\@args)) {
		return @ret;
	}
	while (my @next = _receive) {
		return _return_elements(@next) if _deep_equals(\@next, \@args);
		push @message_cache, \@next;
	}
}

sub receive_nb {
	my @args = @_;
	if (my @ret = _match_mailbox(\@args)) {
		return @ret;
	}
	return if not my @next = _receive_nb;
	return _return_elements(@next) if _deep_equals(\@next, \@args);
	push @message_cache, \@next;
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
	while (my @next = _receive) {
		for my $pair (@table) {
			if (_deep_equals(\@next, $pair->[0])) {
				$pair->[1]->(@next) if defined $pair->[1];
				return _return_elements(@next);
			}
			push @message_cache, \@next;
		}
	}
}

1;

__END__

=head1 NAME

threads::lite - Yet another threads library

=head1 VERSION

Version 0.010

=head1 SYNOPSIS

This module implements threads for perl. One crucial difference with normal threads is that the threads are B<entirely> disconnected, except by message queues (channel). It thus facilitates a message passing style of multi-threading.

=head1 CLASS METHODS

=head3 spawn($options, $sub)

Spawn a new thread. It will run $sub and send all monitoring processes it's return value. $options is a hashref that can contain the following elements.

=over 4

=item monitor => 0/1

If this is true, the calling process will monitor the newly spawned.

=item modules => [...]

Load the specified modules before running any code.

=back

$sub can be a function name or a subref. In the latter case it will be serialized, sent to the new thread. This means that any enclosed variables will probability not work as expected.

=head1 FUNCTIONS

All functions are exported by default.

=head2 Receiving functions

threads::lite exports by default three functions for receiving messages from the process mailbox. Each of them accepts matching patterns used to select those messages. Each of the elements in the pattern will be smartmatched against the appropriate element in the message. The message may contain more elements than the pattern, they will not be included in the matching.

=head3 receive(@pattern)

Return the first message matching pattern @pattern. If there is no such message in the queue, it blocks until a suitable message is received.

=head3 receive_nb(@pattern)

Return the first message matching pattern @pattern. If there is no such message in the queue, it returns an empty list (undef in scalar context).

=head3 receive_table( [@pattern] => action ]...)

Try to match each pattern to the message queue. The first successful pattern is used. If none of the patterns match any of the messages in the queue, it blocks until a suitable message is received.

=head2 Utility functions

=head3 self

Retreive the tid corresponding with the current thread.

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
