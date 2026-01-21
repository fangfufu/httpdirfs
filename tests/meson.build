unity_sub = subproject('unity')
unity_dep = unity_sub.get_variable('unity_dep')

test_util = executable('test_util',
    sources: [
        'test_util.c',
        '../src/util.c',
        '../src/log.c',
        '../src/config.c',
    ],
    dependencies: [
        unity_dep,
        libcurl_dep,
        openssl_dep,
        uuid_dep
    ],
    include_directories: include_directories('../src'),
    c_args: c_args
)

test('test_util', test_util)
