unity_sub = subproject('unity')
unity_dep = unity_sub.get_variable('unity_dep')

lib_src_files = []
foreach f : lib_srcs
    lib_src_files += '../' + f
endforeach

test_deps = [
    unity_dep,
    gumbo_dep,
    libcurl_dep,
    fuse_dep,
    uuid_dep,
    expat_dep,
    openssl_dep,
    execinfo_dep
]

test_util = executable('test_util',
    sources: ['test_util.c'] + lib_src_files,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_util', test_util)

test_cache = executable('test_cache',
    sources: ['test_cache.c'] + lib_src_files,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_cache', test_cache)

test_config = executable('test_config',
    sources: ['test_config.c'] + lib_src_files,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_config', test_config)

test_link = executable('test_link',
    sources: ['test_link.c'] + lib_src_files,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_link', test_link)
