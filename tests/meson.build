unity_sub = subproject('unity')
unity_dep = unity_sub.get_variable('unity_dep')

test_deps = [unity_dep] + httpdirfs_deps

test_util = executable('test_util',
    sources: ['test_util.c'],
    link_with: httpdirfs_lib,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_util', test_util)

test_cache = executable('test_cache',
    sources: ['test_cache.c'],
    link_with: httpdirfs_lib,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_cache', test_cache)

test_config = executable('test_config',
    sources: ['test_config.c'],
    link_with: httpdirfs_lib,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_config', test_config)

test_link = executable('test_link',
    sources: ['test_link.c'],
    link_with: httpdirfs_lib,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_link', test_link)

# Integration test (requires FUSE and Python 3)
integration_test = find_program('integration/run_integration_test.sh',
                                required: false)
if integration_test.found()
    httpdirfs_exe = meson.project_source_root() / 'builddir' / 'httpdirfs'
    test('integration',
         integration_test,
         args: [httpdirfs_exe],
         timeout: 900,
         is_parallel: false,
         suite: 'integration')
endif
