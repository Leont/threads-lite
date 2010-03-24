use 5.010;
use strict;
use warnings;
use Module::Build;
use Config;

if (not $Config{useithreads}) {
	print STDERR "Can't use threads::lite without threading support";
	exit 0;
}

my $builder = Module::Build->new(
	module_name         => 'threads::lite',
	license             => 'perl',
	dist_author         => 'Leon Timmermans <leont@cpan.org>',
	dist_version_from   => 'lib/threads/lite.pm',
	configure_requires  => {
		'Module::Build' => 0,
	},
	build_requires => {
		'Module::Build' => 0,
		'Test::More' => 0,
	},
	requires            => {
		perl            => 5.010,
		Storable        => 2.05,
	},
	c_source            => 'src',
    add_to_cleanup      => [ 'threads-lite-*' ],
	create_makefile_pl  => 'small',
	meta_merge => {
		resources       => {
			bugtracker  => 'http://rt.cpan.org/NoAuth/Bugs.html?Dist=threads-lite',
			repository  => 'http://github.com/Leont/threads-lite/tree',
		},
		keywords       => [ 'threads', 'channels' ],
	},
	sign                => 1,
);

$builder->create_build_script();
