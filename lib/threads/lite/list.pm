package threads::lite::list;

use strict;
use warnings;
use base 'Exporter';

our @EXPORT_OK = qw/paralel_map/;

use threads::lite;

our $VERSION = $threads::lite::VERSION;

our $THREADS = 4;

sub _mapper {
	my (undef, $filter) = receive('filter');
	my $continue = 1;
	while ($continue) {
		receive_table(
			[ qr//, 'map' ] => sub {
				my ($manager, undef, $index, $value) = @_;
				local $_ = $value;
				$manager->send(self, 'map', $index, $filter->());
			},
			[ 'kill' ] => sub {
				$continue = 0;
			},
			[] => sub {
				warn sprintf "Received something unknown: (%s)\n", join ',', @_;
			}
		);
	}
	return;
}

sub _receive_next {
	my $threads = shift;
	my ($thread, undef, $index, @value) = receive($threads, 'map');
	return ($thread, $index, @value);
}

sub new {
	my $class = shift;
	my %options = (
		modules => [],
		threads => $THREADS,
		@_,
	);
	my @modules = ('threads::lite::list', @{ $options{modules} });
	my %threads = map { ( $_->id => $_ ) }
		threads::lite->spawn({ modules => \@modules, monitor => 1 , pool_size => $options{threads}}, 'threads::lite::list::_mapper' );
	$_->send(filter => $options{code}) for values %threads;
	return bless \%threads, $class;
}

sub map {
	my ($self, @args) = @_;
	my ($i, @ret) = 0;

	my $id = self;
	my %threads = %{ $self };
	for my $thread (values %threads) {
		last if $i == @args;
		$thread->send($id, 'map', $i, $args[$i]);
		$i++;
	}
	while ($i < @args) {
		my ($thread, $index, @value) = _receive_next(qr//);
		$ret[$index] = \@value;
		$thread->send($id, 'map', $i, $args[$i]);
		$i++;
	}
	while (%threads) {
		my ($thread, $index, @value) = _receive_next(qr//);
		$ret[$index] = \@value;
		delete $threads{ $thread->id };
	}

	return map { @{$_} } @ret;
}

sub paralel_map(&@) {
	my ($code, @args) = @_;

	my $object = __PACKAGE__->new(code => $code);
	return $object->map(@args);
}

sub DESTROY {
	my $self = shift;
	for my $thread (values %{ $self }) {
		$thread->send('kill');
	}
	return;
}

1;

=head1 NAME

threads::lite::list - Threaded list utilities

=head1 VERSION

Version 0.010

=head1 SYNOPSIS

This module implements threads for perl. One crucial difference with normal threads is that the threads are B<entirely> disconnected, except by message queues (channel). It thus facilitates a message passing style of multi-threading.

=head1 CLASS METHODS

=head1 FUNCTIONS

=head2 paralel_map

XXX

=head2 new

=head2 map

=head1 AUTHOR

Leon Timmermans, C<< <leont at cpan.org> >>

=head1 BUGS

This is an early development release, and is expected to be buggy and incomplete.

Please report any bugs or feature requests to C<bug-threads-lite at rt.cpan.org>, or through
the web interface at L<http://rt.cpan.org/NoAuth/ReportBug.html?Queue=threads-lite>.  I will be notified, and then you'll
automatically be notified of progress on your bug as I make changes.

=head1 SUPPORT

You can find documentation for this module with the perldoc command.

    perldoc threads::lite::list


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
