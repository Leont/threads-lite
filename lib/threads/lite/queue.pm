package threads::lite::queue;

use strict;
use warnings;

use Storable 2.05;
use threads::lite;

sub STORABLE_freeze {
	require Carp;
	Carp::croak "Can't freeze thread queue";
}

1;

__END__


=head1 NAME

threads::lite - The great new threads::lite!

=head1 VERSION

Version 0.01

=head1 SYNOPSIS

Quick summary of what the module does.

Perhaps a little code snippet.

    use threads::lite;

    my $foo = threads::lite->new();
    ...

=head1 EXPORT

A list of functions that can be exported.  You can delete this section
if you don't export anything, such as for a purely object-oriented module.

=head1 FUNCTIONS

=head2 new

=head2 enqueue

=head2 dequeue

=head2 dequeue_nb

=cut

=for comments

=head2 STORABLE_freeze

=cut
