# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- The refreshed LinkTable is now saved.
(https://github.com/fangfufu/httpdirfs/issues/141)
- Only one LinkTable of the same directory is created when the cache mode is
enabled.
(https://github.com/fangfufu/httpdirfs/issues/140)
- Cache mode noe works correctly witht escaped URL.
(https://github.com/fangfufu/httpdirfs/issues/138)
- Fixed ``--user-agent`` option
(https://github.com/fangfufu/httpdirfs/issues/159)

### Added
- Add ``--http-header`` option
(https://github.com/fangfufu/httpdirfs/issues/157)
- Add ``--cache-clear`` option
(https://github.com/fangfufu/httpdirfs/issues/111)

### Changed
- Improved LinkTable caching. LinkTable invalidation is now purely based on
timeout.
(https://github.com/fangfufu/httpdirfs/issues/147)
- Replaced the GNU Autotools based build system with Meson.
(https://github.com/fangfufu/httpdirfs/issues/149)
- Transitioned from using libfuse 2.x to to 3.x.
(https://github.com/fangfufu/httpdirfs/issues/116)
- Updated OpenSSL MD5 checksum generation API usage.
(https://github.com/fangfufu/httpdirfs/issues/143)

## [1.2.5] - 2023-02-24

### Fixed
- No longer compile with UBSAN enabled by default to avoid introducing
security vulnerability.

## [1.2.4] - 2023-01-11

### Added
- Add ``--cacert`` and ``--proxy-cacert`` options

### Fixed
- ``Link_download_full``: don't ``FREE(NULL)``
- Correct error message in ``FREE()``
- Error handling for ``fs_open`` and ``getopt_long``
- Fix IO error with funkwhale subsonic API
- Fix ``--insecure-tls`` in help and README

## [1.2.3] - 2021-08-31

### Added
- Single File Mode, which allows the mounting of a single file in a virtual
directory
- Manual page generation in Makefile.

### Changed
- Improve log / debug output.
- Removed unnecessary mutex lock/unlocks.

### Fixed
- Handling empty files from HTTP server

## [1.2.2] - 2021-08-08
### Fixed
- macOS uninstallation in Makefile.
- Filenames start with percentage encoding are now parsed properly
- For Apache server configured with IconsAreLinks, the duplicated link no longer
shows up.

## [1.2.1] - 2021-05-27
### Added
- macOS compilation support.

## [1.2.0] - 2019-11-01
### Added
- Subsonic server support - this is dedicated to my Debian package maintainer
Jerome Charaoui
- You can now specify which configuration file to use by using the ``--config``
flag.
- Added support for turning off TLS certificate check (``--insecure_tls`` flag).
- Now check for server's support for HTTP range request, which can be turned off
using the ``--no-range-check`` flag.

### Changed
- Wrapped all calloc() calls with error handling functions.
- Various code refactoring

### Fixed
- Remove the erroneous error messages when the user supplies wrong command line
options.
- The same cache folder is used, irrespective whether the server root URL ends
with '/'
- FreeBSD support

## [1.1.10] - 2019-09-10
### Added
- Added a progress indicator for LinkTable_fill().
- Backtrace will now be printed when the program crashes
    - Note that static functions are not included in the printed backtrace!

### Changed
- Updated Makefile, fixed issue #44
    - When header files get changed, the relevant object will get recompiled.
- Improved HTTP temporary failure error handling
    - Now retry on the following HTTP error codes:
        - 429 - Too Many Requests
        - 520 - Cloudflare Unknown Error
        - 524 - Cloudflare Timeout

### Fixed
- No longer deadlock after encountering HTTP 429 while filling up a Linktable.
- LinkTable caching now works again.

## [1.1.9] - 2019-09-02
### Changed
-   Improved the performance of directory listing generation while there are
on-going file transfers
-   Wrapped mutex locking and unlocking functions in error checking functions.

### Fixed
-   Fixed issue #40 - Crashes with "API function called from within callback".
-   Cache system: now keep track of the number of times a cache file has been
opened.
    -   The on-disk cache file no longer gets opened multiple times, if
        a file is opened multiple times. This used to cause inconsistencies
        between two opened cache files.
-   Cache system: Fixed buffer over-read at the boundary.
    -   Say we are using a lock size of 1024k, we send a request for 128k at
    1008k. It won't trigger the download, because we have already downloaded the
    first 1024k at byte 0. So it would read off from the empty disk space!
    -   This problem only occurred during the first time you download a file.
    During subsequent accesses, when you are only reading from the cache, this
    problem did not occur.
-   Cache system: Previously it was possible for Cache_bgdl()'s download offset
    to be modified by the parent thread after the child thread had been
    launched. This used to cause permanent cache file corruption.
-   Cache system: Cache_bgdl() no longer prefetches beyond EOF.
-   Cache system: Data_read() no longer gives warning messages when reaching the
end of the cache file.

## [1.1.8] - 2019-08-24
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
- Now additionally set CURLMOPT_MAX_HOST_CONNECTIONS to limit the amount of
connection HTTPDirFS makes.

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

[Unreleased]: https://github.com/fangfufu/httpdirfs/compare/1.2.5...master
[1.2.5]: https://github.com/fangfufu/httpdirfs/compare/1.2.4...1.2.5
[1.2.4]: https://github.com/fangfufu/httpdirfs/compare/1.2.3...1.2.4
[1.2.3]: https://github.com/fangfufu/httpdirfs/compare/1.2.2...1.2.3
[1.2.2]: https://github.com/fangfufu/httpdirfs/compare/1.2.1...1.2.2
[1.2.1]: https://github.com/fangfufu/httpdirfs/compare/1.2.0...1.2.1
[1.2.0]: https://github.com/fangfufu/httpdirfs/compare/1.1.10...1.2.0
[1.1.10]: https://github.com/fangfufu/httpdirfs/compare/1.1.9...1.1.10
[1.1.9]: https://github.com/fangfufu/httpdirfs/compare/1.1.8...1.1.9
[1.1.8]: https://github.com/fangfufu/httpdirfs/compare/1.1.7...1.1.8
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
