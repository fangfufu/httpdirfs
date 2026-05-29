project('httpdirfs', 'c',
    version: '1.3.2',
    license: 'GPL-3.0-or-later',
    default_options: [
        'buildtype=release',
        'warning_level=3',
        'c_std=c11'
        ]
)

lib_srcs = [
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
    '-fstack-protector-all',
    '-D_FORTIFY_SOURCE=2',
    '-DVERSION="' + meson.project_version() + '"'
]

if get_option('debug')
    c_args += '-DDEBUG'
endif

cc = meson.get_compiler('c')

gumbo_dep = dependency('gumbo')
libcurl_dep = dependency('libcurl')
fuse_dep = dependency('fuse3')
uuid_dep = dependency('uuid')
expat_dep = dependency('expat')
openssl_dep = dependency('openssl')
if cc.has_function('backtrace', prefix: '#include <execinfo.h>')
    execinfo_dep = dependency('', required: false)
else
    execinfo_dep = cc.find_library('execinfo', required: false)
endif

httpdirfs_deps = [
    gumbo_dep,
    libcurl_dep,
    fuse_dep,
    uuid_dep,
    expat_dep,
    openssl_dep,
    execinfo_dep
]

httpdirfs_lib = static_library('httpdirfs_lib',
    lib_srcs,
    dependencies : httpdirfs_deps,
    c_args: c_args,
)

httpdirfs = executable('httpdirfs',
    'src/main.c',
    link_with: httpdirfs_lib,
    dependencies : httpdirfs_deps,
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
			'--no-info',
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

subdir('tests')
