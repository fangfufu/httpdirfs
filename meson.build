project('httpdirfs', 'c',
    version: '1.2.6',
    license: ' GPL-3.0-or-later'
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
    '-g',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Wshadow',
    '-rdynamic',
    '-pthread',
    '-D_GNU_SOURCE',
    '-DVERSION="' + meson.project_version() + '"'
]

gumbo_dep = dependency('gumbo')
libcurl_dep = dependency('libcurl')
fuse_dep = dependency('fuse')
uuid_dep = dependency('uuid')
expat_dep = dependency('expat')
openssl_dep = dependency('openssl')

executable('httpdirfs',
    srcs,
    dependencies : [gumbo_dep, libcurl_dep, fuse_dep, uuid_dep, expat_dep, openssl_dep],
    c_args: c_args,
    install: true,
    install_dir: get_option('bindir')
)