use Modern::Perl;
use threads::lite qw/spawn self receive_match/;

sub child {
	my $other = threads::lite::receive;
	say "Other is $other";
	while (<>) {
		chomp;
		say "read $_";
		$other->send(line => $_);
	}
}

my $self = self;
my $child = spawn({ monitor => 1 } , \&child);
$child->send($self);

my $continue = 1;
while ($continue) {
	say "trying";
	receive_match {
		say "Got @{$_}";
		when([ 'line' ]) {
			my (undef, $line) = @_;
			say "received line: $line";
		}
		when([ 'exit', qr//, $child->id ]) {
			say "received end of file";
			$continue = 0;
		}
		default {
			die sprintf "Got unknown message: (%s)", join ", ", @_;
		}
	};
}
