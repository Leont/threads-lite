use strict;
use warnings;
use Module::Build;

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
	create_makefile_pl  => 'passthrough',
	sign                => 1,
);

$builder->create_build_script();
