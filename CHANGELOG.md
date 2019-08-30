# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.8]
### Changed
- Suppressed "-Wunused-function" in ``network.c`` for network related functions.

### Fixed
- Addressed the link ordering problem raised in issue #28

## [1.1.7] - 2019-08-23
### Added
- Debugging output associated with the mutexes

### Fixed
- Fixed issue #34 - file / directory detection problem
- Fixed issue #36 - hanging when HTTP/2 is used
- Added pthread_detach() for thread cleanup

## [1.1.6] - 2019-05-07
### Changed
- Now set a default cache directory
- path_append() now check for both the existing path and appended path for '/'.
- Now additionally set CURLMOPT_MAX_HOST_CONNECTIONS to limit the amount of connection HTTPDirFS makes. 

## [1.1.5] - 2019-04-26
### Added
- Added ``--dl-seg-size`` command line option.
- Added ``--max-seg-count`` command line option.
- Added ``--max-conns`` command line option.
- Added ``--user-agent`` command line option.
- Added ``--retry-wait`` command line option.

### Changed
- Refactored ``curl_multi_perform_once()`` for lower CPU usage.

## [1.1.4] - 2019-04-26
### Fixed
- Invalid link rechecking after loading a LinkTable from the disk.

## [1.1.3] - 2019-04-26
### Added
- Now handles HTTP 429 Too Many Requests correctly.
- When loading a LinkTable from the hard disk, if there are invalid links in
the LinkTable, their headers are redownloaded for rechecking.

## [1.1.2] - 2019-04-25
### Added
- Now caches directory structure on the hard disk. Httpdirfs no longer stutters
when visiting the directories that had been visited before, after restart. If
there is inconsistency between the number of files on the server and in the
cache, the local directory structure will be recreated, hence the cache will be
refreshed. Creating local directory structure involves downloading the header
for each file, this is what causes stuttering when visiting a new directory.

### Fixed
- Fix typos in recent README changes.
- Update outdated unreleased diff link.

## [1.1.1] - 2019-04-24
### Added
- ``Cache_bgdl()`` to download the next segment in background, after blocks from
the second half of the current segment has been requested.

### Changed
- Changed the ``DATA_BLK_SZ`` to 8MB

## [1.1.0] - 2019-04-23
### Added
- Permanent cache feature.

### Fixed
- Fixed memory leak during LinkTable creation.

## [1.0.4] - 2019-04-12
### Changed
- Enabled HTTP pipelining for performance improved
- Decreased maximum connection number to 10, to reduce the stress to remove
server.

## [1.0.3] - 2019-04-08
### Fixed
- Fixed issue #24 - httpdirfs now opens directories with long listings properly.

## [1.0.2] - 2019-03-02
### Changed
- Closed issue #23 - Dotfile madness, httpdirfs now reads configuration file
from
${XDG_CONFIG_HOME}/httpdirfs, rather than ${HOME}/.httpdirfs

## [1.0.1] - 2019-01-25
- Initial Debian package release

### Added
- Add a manpage

### Changed
- Fix output of ``--version``/``-V``
- Improved Makefile
- Various other minor improvements

## [1.0] - 2018-08-22
- Initial release, everything works correctly, as far as I know.

[Unreleased]: https://github.com/fangfufu/httpdirfs/compare/1.1.7...HEAD
[1.1.7]: https://github.com/fangfufu/httpdirfs/compare/1.1.6...1.1.7
[1.1.6]: https://github.com/fangfufu/httpdirfs/compare/1.1.5...1.1.6
[1.1.5]: https://github.com/fangfufu/httpdirfs/compare/1.1.4...1.1.5
[1.1.4]: https://github.com/fangfufu/httpdirfs/compare/1.1.3...1.1.4
[1.1.3]: https://github.com/fangfufu/httpdirfs/compare/1.1.2...1.1.3
[1.1.2]: https://github.com/fangfufu/httpdirfs/compare/1.1.1...1.1.2
[1.1.1]: https://github.com/fangfufu/httpdirfs/compare/1.1.0...1.1.1
[1.1.0]: https://github.com/fangfufu/httpdirfs/compare/1.0.4...1.1.0
[1.0.4]: https://github.com/fangfufu/httpdirfs/compare/1.0.3...1.0.4
[1.0.3]: https://github.com/fangfufu/httpdirfs/compare/1.0.2...1.0.3
[1.0.2]: https://github.com/fangfufu/httpdirfs/compare/v1.0.1...1.0.2
[1.0.1]: https://github.com/fangfufu/httpdirfs/compare/1.0...v1.0.1
[1.0]: https://github.com/fangfufu/httpdirfs/releases/tag/1.0
