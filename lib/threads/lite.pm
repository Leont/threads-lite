package threads::lite;

use strict;
use warnings;

our $VERSION = '0.01';

use 5.010;

use base qw/DynaLoader Exporter/;
use Storable 2.05;

threads::lite->bootstrap($VERSION);

##no critic ProhibitAutomaticExportation
our @EXPORT    = qw/receive/;
our @EXPORT_OK = qw/send/;

sub _receive;
sub _receive_nb;

my @message_cache;

sub _deep_equals {
	my ($message, $criterion) = @_;
	return if $#{$message} < $#{$criterion};
	for my $i (0..$#{$criterion}) {
		return if $message->[$i] !~~ $criterion->[$i];
	}
	return 1;
}

sub _return_elements {
	return wantarray ? @_ : $_[0];
}

sub _match_mailbox {
	my ($criterion) = @_;
	MESSAGE:
	for my $i (0..$#message_cache) {
		my $message = $message_cache[$i];
		for my $j (0..$#{$criterion}) {
			next MESSAGE if $message->[$j] !~~ $criterion->[$j];
		}
		return _return_elements(@{ splice @message_cache, $i, 1 })
	}
	return;
}

sub create {
	my ($class, @args) = @_;
	my $thread = $class->_create;
	for my $arg (@args) {
		$thread->send(@{$arg});
	}
	return $thread;
}

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

	push @table, [ splice @args, 0, 2 ] while @args > 2;

	for my $pair (@table) {
		if (my @ret = _match_mailbox($pair->[0])) {
			$pair->[1]->(_return_elements(@ret)) if defined $pair->[1];
			return @ret;
		}
	}
	while (my @next = _receive) {
		for my $pair (@table) {
			if (_deep_equals(\@next, $pair->[0])) {
				my @ret = _return_elements(@next);
				$pair->[1]->(@ret) if defined $pair->[1];
				return @ret;
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

Version 0.01

=head1 SYNOPSIS

This module implements threads for perl. One crucial difference with normal threads is that the threads are B<entirely> disconnected, except by thread queues (channel). It thus facilitates a messsage passing style of multi-threading.

=head1 FUNCTIONS

=head2 create

=head2 send

=head2 receive

=head2 receive_nb

=head2 receive_table

=head1 AUTHOR

Leon Timmermans, C<< <leont at cpan.org> >>

=head1 BUGS

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


=head1 ACKNOWLEDGEMENTS


=head1 COPYRIGHT & LICENSE

Copyright 2009 Leon Timmermans, all rights reserved.

This program is free software; you can redistribute it and/or modify it
under the same terms as Perl itself.

=cut

=for ignore

=head2 STORABLE_freeze

=cut
