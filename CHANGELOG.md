# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Added macOS build support and automated CI pipeline
  ([200f70e](https://github.com/fangfufu/httpdirfs/commit/200f70e)).
- Added `--invalid-refresh` flag to refresh invalid links during directory reads
  ([68c305c](https://github.com/fangfufu/httpdirfs/commit/68c305c))
  (https://github.com/fangfufu/httpdirfs/issues/175).
- Added `--zero-len-is-dir` flag to treat zero-length files as directories
  ([9e750b9](https://github.com/fangfufu/httpdirfs/commit/9e750b9)).
- Added support for directory modified times and zero-length files
  ([2fde399](https://github.com/fangfufu/httpdirfs/commit/2fde399),
  [00a6b35](https://github.com/fangfufu/httpdirfs/commit/00a6b35)).
- Added `pre-commit` framework integration with Prettier and Clang-Tidy for code
  quality and formatting
  ([f557a2d](https://github.com/fangfufu/httpdirfs/commit/f557a2d)).
- Added `USAGE.md` for detailed command-line flag documentation
  ([c85c065](https://github.com/fangfufu/httpdirfs/commit/c85c065)).
- Added help text for `-f`, `-s`, and `--debug` flags
  ([553745b](https://github.com/fangfufu/httpdirfs/commit/553745b)).
- Added `codespell` to `pre-commit` and fixed spelling errors
  ([a898adf](https://github.com/fangfufu/httpdirfs/commit/a898adf)).
- Added Repology badge to documentation
  ([996b925](https://github.com/fangfufu/httpdirfs/commit/996b925)).
- Added a `pre-commit` GitHub workflow for automated checks
  ([c5fc5d0](https://github.com/fangfufu/httpdirfs/commit/c5fc5d0)).

### Changed

- Transitioned background thread synchronization from mutexes to semaphores for
  better reliability
  ([4ee7476](https://github.com/fangfufu/httpdirfs/commit/4ee7476))
  (https://github.com/fangfufu/httpdirfs/issues/91).
- Improved error messages and debug output throughout the application
  ([405d097](https://github.com/fangfufu/httpdirfs/commit/405d097)).
- Enhanced static analysis enforcement with comprehensive Clang-Tidy checks
  ([9ae4f89](https://github.com/fangfufu/httpdirfs/commit/9ae4f89)).
- Updated documentation and build requirements for Debian 13 "Trixie"
  ([f44d040](https://github.com/fangfufu/httpdirfs/commit/f44d040)).
- Refined filename character restrictions for improved compatibility
  ([6476571](https://github.com/fangfufu/httpdirfs/commit/6476571))
  (https://github.com/fangfufu/httpdirfs/issues/178).
- Updated `help2man` options to exclude info pages
  ([2ebd190](https://github.com/fangfufu/httpdirfs/commit/2ebd190)).
- Refactored nested `if` statements for improved code conciseness
  ([43906c9](https://github.com/fangfufu/httpdirfs/commit/43906c9)).
- Simplified safety checks in `LinkTable_uninitialised_fill`
  ([f593bba](https://github.com/fangfufu/httpdirfs/commit/f593bba)).
- Allowed `FREE(NULL)` to match standard C `free()` behavior
  ([5dc541e](https://github.com/fangfufu/httpdirfs/commit/5dc541e)).
- Replaced `astyle` with `clang-format` for consistent code styling
  ([a134774](https://github.com/fangfufu/httpdirfs/commit/a134774)).
- Updated documentation with comprehensive `src/README.md` and pre-commit
  instructions
  ([04a8558](https://github.com/fangfufu/httpdirfs/commit/04a8558)).
- Configured Prettier to prevent merging GitHub alert tags in documentation
  ([cea9e65](https://github.com/fangfufu/httpdirfs/commit/cea9e65)).
- Integrated Prettier hook for Markdown formatting
  ([ee1c1fe](https://github.com/fangfufu/httpdirfs/commit/ee1c1fe)).
- Refactored `Cache_read1` to `Cache_read_segment` and made it static
  ([c3c2912](https://github.com/fangfufu/httpdirfs/commit/c3c2912)).
- Optimized network activity by avoiding polling when a response is already
  available ([a77a2f6](https://github.com/fangfufu/httpdirfs/commit/a77a2f6)).

### Fixed

- Resolved macOS FUSE API compatibility issues and compilation errors
  ([200f70e](https://github.com/fangfufu/httpdirfs/commit/200f70e)).
- Fixed various race conditions and potential deadlocks in thread management
  ([42dd71e](https://github.com/fangfufu/httpdirfs/commit/42dd71e))
  (https://github.com/fangfufu/httpdirfs/issues/174).
- Fixed issues with duplicated file entries in certain directory listings
  ([9424389](https://github.com/fangfufu/httpdirfs/commit/9424389))
  (https://github.com/fangfufu/httpdirfs/issues/176).
- Fixed cache corruption occurring on write errors
  ([fe1cce4](https://github.com/fangfufu/httpdirfs/commit/fe1cce4)).
- Fixed Nginx-specific directory listing parsing issues
  ([3527f52](https://github.com/fangfufu/httpdirfs/commit/3527f52)).
- Fixed `const`-correctness for `strchr`/`strrchr` wrappers
  ([92622fc](https://github.com/fangfufu/httpdirfs/commit/92622fc)).
- Fixed link name comparison logic (`linknames_equal`)
  ([4c1a285](https://github.com/fangfufu/httpdirfs/commit/4c1a285)).
- Fixed issue #95: Avoid infinite hang when accessing directories returning 404
  errors ([ed5e9fa](https://github.com/fangfufu/httpdirfs/commit/ed5e9fa))
  (https://github.com/fangfufu/httpdirfs/issues/95).
- Fixed issue #190: Implemented robust retry logic for network downloads
  ([08a7987](https://github.com/fangfufu/httpdirfs/commit/08a7987))
  (https://github.com/fangfufu/httpdirfs/pull/203).
- Fixed issue #193: Support parsing links to intermediate subdirectories
  (https://github.com/fangfufu/httpdirfs/issues/193).
- Fixed security vulnerability V-001
  ([4584847](https://github.com/fangfufu/httpdirfs/commit/4584847)).

### [1.2.7] - 2024-11-11

### Fixed

- Fixed FreeBSD compilation
  ([43bdf7e](https://github.com/fangfufu/httpdirfs/commit/43bdf7e))
  (https://github.com/fangfufu/httpdirfs/issues/165)

## [1.2.6] - 2024-10-07

### Fixed

- The refreshed LinkTable is now saved
  ([9135168](https://github.com/fangfufu/httpdirfs/commit/9135168)).
  (https://github.com/fangfufu/httpdirfs/issues/141)
- Only one LinkTable of the same directory is created when the cache mode is
  enabled ([e253b4a](https://github.com/fangfufu/httpdirfs/commit/e253b4a)).
  (https://github.com/fangfufu/httpdirfs/issues/140)
- Cache mode now works correctly with escaped URL
  ([720db5a](https://github.com/fangfufu/httpdirfs/commit/720db5a)).
  (https://github.com/fangfufu/httpdirfs/issues/138)
- Fixed `--user-agent` option
  ([8f0ef15](https://github.com/fangfufu/httpdirfs/commit/8f0ef15))
  (https://github.com/fangfufu/httpdirfs/issues/159)
- Allow leading `./` segments in links
  ([bd33966](https://github.com/fangfufu/httpdirfs/commit/bd33966))
  (https://github.com/fangfufu/httpdirfs/pull/125)
- Fix segfault due to missing LinkTable allocation
  ([1f91111](https://github.com/fangfufu/httpdirfs/commit/1f91111))
  (https://github.com/fangfufu/httpdirfs/pull/155)
- Fix Segmentation fault if HOME not set
  ([09ebc82](https://github.com/fangfufu/httpdirfs/commit/09ebc82))
  (https://github.com/fangfufu/httpdirfs/pull/162)
- Handle sites that use absolute links and sites that require the final slash
  ([4d323b8](https://github.com/fangfufu/httpdirfs/commit/4d323b8),
  [41cb4b8](https://github.com/fangfufu/httpdirfs/commit/41cb4b8))
  (https://github.com/fangfufu/httpdirfs/pull/121)

### Added

- Add `--http-header` option
  ([e3b2904](https://github.com/fangfufu/httpdirfs/commit/e3b2904))
  (https://github.com/fangfufu/httpdirfs/issues/157)
- Add `--cache-clear` option
  ([194a10f](https://github.com/fangfufu/httpdirfs/commit/194a10f))
  (https://github.com/fangfufu/httpdirfs/issues/111)
- Add `--refresh-timeout` to set refresh timeout for directory contents
  ([a309994](https://github.com/fangfufu/httpdirfs/commit/a309994))
  (https://github.com/fangfufu/httpdirfs/pull/114)

### Changed

- Improved LinkTable caching. LinkTable invalidation is now purely based on
  timeout ([0747566](https://github.com/fangfufu/httpdirfs/commit/0747566)).
  (https://github.com/fangfufu/httpdirfs/issues/147)
- Replaced the GNU Autotools based build system with Meson
  ([63c03a5](https://github.com/fangfufu/httpdirfs/commit/63c03a5)).
  (https://github.com/fangfufu/httpdirfs/issues/149)
- Transitioned from using libfuse 2.x to to 3.x
  ([ad34ae0](https://github.com/fangfufu/httpdirfs/commit/ad34ae0)).
  (https://github.com/fangfufu/httpdirfs/issues/116)
- Updated OpenSSL MD5 checksum generation API usage
  ([86c1185](https://github.com/fangfufu/httpdirfs/commit/86c1185)).
  (https://github.com/fangfufu/httpdirfs/issues/143)

## [1.2.5] - 2023-02-24

### Fixed

- No longer compile with UBSAN enabled by default to avoid introducing security
  vulnerability
  ([fe45afc](https://github.com/fangfufu/httpdirfs/commit/fe45afc)).

## [1.2.4] - 2023-01-11

### Added

- Add `--cacert` and `--proxy-cacert` options
  ([12abb7d](https://github.com/fangfufu/httpdirfs/commit/12abb7d))

### Fixed

- `Link_download_full`: don't `FREE(NULL)`
  ([ff5f566](https://github.com/fangfufu/httpdirfs/commit/ff5f566))
- Correct error message in `FREE()`
  ([833cbf9](https://github.com/fangfufu/httpdirfs/commit/833cbf9))
- Error handling for `fs_open` and `getopt_long`
  ([72d15ab](https://github.com/fangfufu/httpdirfs/commit/72d15ab),
  [ffb2658](https://github.com/fangfufu/httpdirfs/commit/ffb2658))
- Fix IO error with funkwhale subsonic API
  ([abef0c9](https://github.com/fangfufu/httpdirfs/commit/abef0c9))
- Fix `--insecure-tls` in help and README
  ([ebcfb0a](https://github.com/fangfufu/httpdirfs/commit/ebcfb0a))

## [1.2.3] - 2021-08-31

### Added

- Single File Mode, which allows the mounting of a single file in a virtual
  directory ([f42264d](https://github.com/fangfufu/httpdirfs/commit/f42264d))
- Manual page generation in Makefile
  ([ff97740](https://github.com/fangfufu/httpdirfs/commit/ff97740),
  [07603c3](https://github.com/fangfufu/httpdirfs/commit/07603c3)).

### Changed

- Improve log / debug output
  ([07603c3](https://github.com/fangfufu/httpdirfs/commit/07603c3),
  [e02042c](https://github.com/fangfufu/httpdirfs/commit/e02042c),
  [f791ceb](https://github.com/fangfufu/httpdirfs/commit/f791ceb)).
- Removed unnecessary mutex lock/unlocks
  ([7813487](https://github.com/fangfufu/httpdirfs/commit/7813487)).

### Fixed

- Handling empty files from HTTP server
  ([3c7e790](https://github.com/fangfufu/httpdirfs/commit/3c7e790))

## [1.2.2] - 2021-08-08

### Fixed

- macOS uninstallation in Makefile
  ([92a7330](https://github.com/fangfufu/httpdirfs/commit/92a7330)).
- Filenames start with percentage encoding are now parsed properly
  ([861481e](https://github.com/fangfufu/httpdirfs/commit/861481e))
- For Apache server configured with IconsAreLinks, the duplicated link no longer
  shows up ([e76b079](https://github.com/fangfufu/httpdirfs/commit/e76b079)).

## [1.2.1] - 2021-05-27

### Added

- macOS compilation support
  ([e553463](https://github.com/fangfufu/httpdirfs/commit/e553463),
  [1df02b0](https://github.com/fangfufu/httpdirfs/commit/1df02b0)).

## [1.2.0] - 2019-11-01

### Added

- Subsonic server support - this is dedicated to my Debian package maintainer
  Jerome Charaoui
  ([8206b4f](https://github.com/fangfufu/httpdirfs/commit/8206b4f),
  [5062f51](https://github.com/fangfufu/httpdirfs/commit/5062f51))
- You can now specify which configuration file to use by using the `--config`
  flag ([4b02980](https://github.com/fangfufu/httpdirfs/commit/4b02980)).
- Added support for turning off TLS certificate check (`--insecure_tls` flag)
  ([e9c8689](https://github.com/fangfufu/httpdirfs/commit/e9c8689)).
- Now check for server's support for HTTP range request, which can be turned off
  using the `--no-range-check` flag
  ([a8ef8c8](https://github.com/fangfufu/httpdirfs/commit/a8ef8c8),
  [c2be88c](https://github.com/fangfufu/httpdirfs/commit/c2be88c)).

### Changed

- Wrapped all calloc() calls with error handling functions
  ([b7c63f4](https://github.com/fangfufu/httpdirfs/commit/b7c63f4)).
- Various code refactoring.

### Fixed

- Remove the erroneous error messages when the user supplies wrong command line
  options ([dec32b0](https://github.com/fangfufu/httpdirfs/commit/dec32b0)).
- The same cache folder is used, irrespective whether the server root URL ends
  with '/' ([6d14497](https://github.com/fangfufu/httpdirfs/commit/6d14497)).
- FreeBSD support
  ([80c0b69](https://github.com/fangfufu/httpdirfs/commit/80c0b69),
  [b177039](https://github.com/fangfufu/httpdirfs/commit/b177039)).

## [1.1.10] - 2019-09-10

### Added

- Added a progress indicator for LinkTable_fill()
  ([9ff099c](https://github.com/fangfufu/httpdirfs/commit/9ff099c)).
- Backtrace will now be printed when the program crashes
  ([c7dfa24](https://github.com/fangfufu/httpdirfs/commit/c7dfa24))
  - Note that static functions are not included in the printed backtrace!

### Changed

- Updated Makefile, fixed issue #44
  ([765f4e0](https://github.com/fangfufu/httpdirfs/commit/765f4e0))
  - When header files get changed, the relevant object will get recompiled.
- Improved HTTP temporary failure error handling
  ([1493190](https://github.com/fangfufu/httpdirfs/commit/1493190),
  [ff67794](https://github.com/fangfufu/httpdirfs/commit/ff67794))
  - Now retry on the following HTTP error codes:
    - 429 - Too Many Requests
    - 520 - Cloudflare Unknown Error
    - 524 - Cloudflare Timeout

### Fixed

- No longer deadlock after encountering HTTP 429 while filling up a Linktable
  ([b6777c0](https://github.com/fangfufu/httpdirfs/commit/b6777c0)).
- LinkTable caching now works again
  ([bc23ee0](https://github.com/fangfufu/httpdirfs/commit/bc23ee0)).

## [1.1.9] - 2019-09-02

### Changed

- Improved the performance of directory listing generation while there are
  on-going file transfers
  ([afb2a8f](https://github.com/fangfufu/httpdirfs/commit/afb2a8f))
- Wrapped mutex locking and unlocking functions in error checking functions
  ([1a44a4d](https://github.com/fangfufu/httpdirfs/commit/1a44a4d),
  [e06ea6d](https://github.com/fangfufu/httpdirfs/commit/e06ea6d)).

### Fixed

- Fixed issue #40 - Crashes with "API function called from within callback"
  ([d6fbcb4](https://github.com/fangfufu/httpdirfs/commit/d6fbcb4)).
- Cache system: now keep track of the number of times a cache file has been
  opened ([9229658](https://github.com/fangfufu/httpdirfs/commit/9229658),
  [ed5457c](https://github.com/fangfufu/httpdirfs/commit/ed5457c)). - The
  on-disk cache file no longer gets opened multiple times, if a file is opened
  multiple times. This used to cause inconsistencies between two opened cache
  files.
- Cache system: Fixed buffer over-read at the boundary
  ([6c8a15d](https://github.com/fangfufu/httpdirfs/commit/6c8a15d)).
  - Say we are using a lock size of 1024k, we send a request for 128k at 1008k.
    It won't trigger the download, because we have already downloaded the first
    1024k at byte 0. So it would read off from the empty disk space!
  - This problem only occurred during the first time you download a file. During
    subsequent accesses, when you are only reading from the cache, this problem
    did not occur.
- Cache system: Previously it was possible for Cache_bgdl()'s download offset to
  be modified by the parent thread after the child thread had been launched
  ([9e3e474](https://github.com/fangfufu/httpdirfs/commit/9e3e474)). This used
  to cause permanent cache file corruption.
- Cache system: Cache_bgdl() no longer prefetches beyond EOF
  ([4c0b7da](https://github.com/fangfufu/httpdirfs/commit/4c0b7da)).
- Cache system: Data_read() no longer gives warning messages when reaching the
  end of the cache file
  ([ee397d1](https://github.com/fangfufu/httpdirfs/commit/ee397d1)).

## [1.1.8] - 2019-08-30

### Changed

- Suppressed "-Wunused-function" in `network.c` for network related functions
  ([20577e5](https://github.com/fangfufu/httpdirfs/commit/20577e5)).

### Fixed

- Addressed the link ordering problem raised in issue #28
  ([97ecbff](https://github.com/fangfufu/httpdirfs/commit/97ecbff))

## [1.1.7] - 2019-08-23

### Added

- Debugging output associated with the mutexes
  ([c660159](https://github.com/fangfufu/httpdirfs/commit/c660159))

### Fixed

- Fixed issue #34 - file / directory detection problem
  ([78d8167](https://github.com/fangfufu/httpdirfs/commit/78d8167))
- Fixed issue #36 - hanging when HTTP/2 is used
  ([ed37aa5](https://github.com/fangfufu/httpdirfs/commit/ed37aa5))
- Added pthread_detach() for thread cleanup
  ([c72b0d4](https://github.com/fangfufu/httpdirfs/commit/c72b0d4))

## [1.1.6] - 2019-05-07

### Changed

- Now set a default cache directory
  ([2835201](https://github.com/fangfufu/httpdirfs/commit/2835201))
- path_append() now check for both the existing path and appended path for '/'
  ([88efbdf](https://github.com/fangfufu/httpdirfs/commit/88efbdf)).
- Now additionally set CURLMOPT_MAX_HOST_CONNECTIONS to limit the amount of
  connection HTTPDirFS makes
  ([f1c7e6e](https://github.com/fangfufu/httpdirfs/commit/f1c7e6e)).

## [1.1.5] - 2019-04-27

### Added

- Added `--dl-seg-size` command line option
  ([ba8c723](https://github.com/fangfufu/httpdirfs/commit/ba8c723)).
- Added `--max-seg-count` command line option
  ([4acf91a](https://github.com/fangfufu/httpdirfs/commit/4acf91a)).
- Added `--max-conns` command line option
  ([85d66ad](https://github.com/fangfufu/httpdirfs/commit/85d66ad)).
- Added `--user-agent` command line option
  ([85d66ad](https://github.com/fangfufu/httpdirfs/commit/85d66ad)).
- Added `--retry-wait` command line option
  ([7c1c1d2](https://github.com/fangfufu/httpdirfs/commit/7c1c1d2)).

### Changed

- Refactored `curl_multi_perform_once()` for lower CPU usage
  ([2a2ac2d](https://github.com/fangfufu/httpdirfs/commit/2a2ac2d)).

## [1.1.4] - 2019-04-26

### Fixed

- Invalid link rechecking after loading a LinkTable from the disk
  ([1dc54af](https://github.com/fangfufu/httpdirfs/commit/1dc54af)).

## [1.1.3] - 2019-04-26

### Added

- Now handles HTTP 429 Too Many Requests correctly
  ([39820e3](https://github.com/fangfufu/httpdirfs/commit/39820e3),
  [774f14c](https://github.com/fangfufu/httpdirfs/commit/774f14c)).
- When loading a LinkTable from the hard disk, if there are invalid links in the
  LinkTable, their headers are redownloaded for rechecking
  ([9717d01](https://github.com/fangfufu/httpdirfs/commit/9717d01)).

## [1.1.2] - 2019-04-26

### Added

- Now caches directory structure on the hard disk
  ([5f6fc3f](https://github.com/fangfufu/httpdirfs/commit/5f6fc3f),
  [9a5f37d](https://github.com/fangfufu/httpdirfs/commit/9a5f37d)). Httpdirfs no
  longer stutters when visiting the directories that had been visited before,
  after restart. If there is inconsistency between the number of files on the
  server and in the cache, the local directory structure will be recreated,
  hence the cache will be refreshed. Creating local directory structure involves
  downloading the header for each file, this is what causes stuttering when
  visiting a new directory.

## [1.1.1] - 2019-04-25

### Added

- `Cache_bgdl()` to download the next segment in background, after blocks from
  the second half of the current segment has been requested
  ([e442871](https://github.com/fangfufu/httpdirfs/commit/e442871),
  [df025b1](https://github.com/fangfufu/httpdirfs/commit/df025b1)).

### Changed

- Changed the `DATA_BLK_SZ` to 8MB
  ([6d02b05](https://github.com/fangfufu/httpdirfs/commit/6d02b05))

## [1.1.0] - 2019-04-24

### Added

- Permanent cache feature
  ([e2d2b0d](https://github.com/fangfufu/httpdirfs/commit/e2d2b0d)).

### Fixed

- Fixed memory leak during LinkTable creation
  ([77bb715](https://github.com/fangfufu/httpdirfs/commit/77bb715)).

## [1.0.4] - 2019-04-12

### Changed

- Enabled HTTP pipelining for performance improved
  ([3f15be0](https://github.com/fangfufu/httpdirfs/commit/3f15be0))
- Decreased maximum connection number to 10, to reduce the stress to remove
  server ([3f15be0](https://github.com/fangfufu/httpdirfs/commit/3f15be0)).

## [1.0.3] - 2019-04-08

### Fixed

- Fixed issue #24 - httpdirfs now opens directories with long listings properly
  ([329abda](https://github.com/fangfufu/httpdirfs/commit/329abda)).

## [1.0.2] - 2019-03-02

### Changed

- Closed issue #23 - Dotfile madness, httpdirfs now reads configuration file
  from ${XDG_CONFIG_HOME}/httpdirfs, rather than ${HOME}/.httpdirfs
  ([b862c4b](https://github.com/fangfufu/httpdirfs/commit/b862c4b))

## [1.0.1] - 2019-01-25

- Initial Debian package release

### Added

- Add a manpage
  ([740acee](https://github.com/fangfufu/httpdirfs/commit/740acee))

### Changed

- Fix output of `--version`/`-V`
  ([3e1675d](https://github.com/fangfufu/httpdirfs/commit/3e1675d))
- Improved Makefile
  ([92d4b0a](https://github.com/fangfufu/httpdirfs/commit/92d4b0a),
  [d73b16d](https://github.com/fangfufu/httpdirfs/commit/d73b16d))
- Various other minor improvements
  ([77f4989](https://github.com/fangfufu/httpdirfs/commit/77f4989),
  [3b5252f](https://github.com/fangfufu/httpdirfs/commit/3b5252f))

## [1.0] - 2018-08-22

- Initial release, everything works correctly, as far as I know.

[Unreleased]: https://github.com/fangfufu/httpdirfs/compare/1.2.7...master
[1.2.7]: https://github.com/fangfufu/httpdirfs/compare/1.2.6...1.2.7
[1.2.6]: https://github.com/fangfufu/httpdirfs/compare/1.2.5...1.2.6
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
