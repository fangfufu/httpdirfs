add_test_setup('default',
               exclude_suites: ['integration_long'],
               is_default: true)

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
test('test_util', test_util, suite: 'unit_test')

test_cache = executable('test_cache',
    sources: ['test_cache.c'],
    link_with: httpdirfs_lib,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_cache', test_cache, suite: 'unit_test')

test_config = executable('test_config',
    sources: ['test_config.c'],
    link_with: httpdirfs_lib,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_config', test_config, suite: 'unit_test')

test_link = executable('test_link',
    sources: ['test_link.c'],
    link_with: httpdirfs_lib,
    dependencies: test_deps,
    include_directories: include_directories('../src'),
    c_args: c_args
)
test('test_link', test_link, suite: 'unit_test')

# Integration test (requires FUSE and Python 3)
integration_test = find_program('integration/run_integration_test.sh',
                                required: false)
if integration_test.found()
    test('integration_short',
         integration_test,
         args: ['--short', httpdirfs],
         timeout: 300,
         is_parallel: false,
         suite: 'integration_short')

    test('integration_long',
         integration_test,
         args: ['--long', httpdirfs],
         timeout: 900,
         is_parallel: false,
         suite: 'integration_long')
endif
