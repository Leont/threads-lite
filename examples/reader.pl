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
