#! perl

use strict;
use warnings;

use Test::More tests => 1;
use Test::Exception;
use threads::lite;

lives_ok { my $thread = threads::lite->spawn(undef, sub { 42 } ) } "Passing undef as an option should be handled properly";

