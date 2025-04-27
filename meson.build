project('httpdirfs', 'c',
    version: '1.2.7',
    license: 'GPL-3.0-or-later',
    default_options: [
        'buildtype=release',
        'werror=true',
        'warning_level=3'
        ]
)

srcs = [
    'src/main.c',
    'src/network.c',
    'src/fuse_local.c',
    'src/link.c',
    'src/cache.c',
    'src/util.c',
    'src/sonic.c',
    'src/log.c',
    'src/config.c',
    'src/memcache.c'
]

c_args = [
    '-Wpedantic',
    '-Wstrict-prototypes',
    '-Wshadow',
    '-pthread',
    '-D_GNU_SOURCE',
    '-DVERSION="' + meson.project_version() + '"'
]

cc = meson.get_compiler('c')

gumbo_dep = dependency('gumbo')
libcurl_dep = dependency('libcurl')
fuse_dep = dependency('fuse3')
uuid_dep = dependency('uuid')
expat_dep = dependency('expat')
openssl_dep = dependency('openssl')
execinfo_dep = cc.find_library('execinfo', required: false)

httpdirfs = executable('httpdirfs',
    srcs,
    dependencies : [gumbo_dep, libcurl_dep, fuse_dep, uuid_dep, expat_dep, openssl_dep, execinfo_dep],
    c_args: c_args,
    install: true,
    install_dir: get_option('bindir'),
)

if (host_machine.system() == 'linux')
    help2man = find_program('help2man', required: false)
	if help2man.found()
		help2man_opts = [
			'--name="mount HTTP directory as a virtual filesystem"',
			'--no-discard-stderr',
			'--section=1']
		manfile = custom_target('httpdirfs.1',
			output: 'httpdirfs.1',
			command: [help2man, help2man_opts, '--output=@OUTPUT@', httpdirfs],
			install: true,
			install_dir: get_option('mandir') / 'man1')
	endif
endif

run_target('format',
  command : 'scripts/format.sh')

run_target('doxygen',
  command : 'scripts/doxygen.sh')
