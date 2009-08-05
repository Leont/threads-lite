package threads::lite;

use strict;
use warnings;

our $VERSION = '0.01';

use threads::lite::queue;
use base qw/DynaLoader Exporter/;
use Storable 2.05;

threads::lite->bootstrap($VERSION);

##no critic ProhibitAutomaticExportation
our @EXPORT    = qw/receive/;
our @EXPORT_OK = qw/send/;

sub _receive;

my @message_cache;

sub _deep_equals {
	my ($message, $criterion) = @_;
	for my $i (0..$#{$criterion}) {
		return if $#{$message} < $i or $message->[$i] ne $criterion->[$i];
	}
	return 1;
}

sub receive {
	my @args = @_;
	for my $i (0..$#message_cache) {
		return splice @message_cache, $i, 1 if _deep_equals($message_cache[$i], \@args);
	}
	while (my @next = _receive) {
		return wantarray ? @next : $next[0] if _deep_equals(\@next, \@args);
		push @message_cache, \@next;
	}
}

sub _run {
	warn "# Running!\n";
	my @args = threads::lite::receive();
	return;
}

sub STORABLE_freeze {
	require Carp;
	Carp::croak 'Can\'t freeze thread queue';
}

1;

__END__

=head1 NAME

threads::lite - Yet another threads library

=head1 VERSION

Version 0.01

=head1 SYNOPSIS

This module implements threads for perl. One crucial difference with normal threads is that the threads are B<entirely> disconnected, except by thread queues (channal). It thus facilitates a messsage passing style of multi-threading.

=head1 FUNCTIONS

=head2 create

=head2 send

=head2 receive

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
