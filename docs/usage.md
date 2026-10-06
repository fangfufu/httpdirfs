## HTTPDirFS Usage Guide

HTTPDirFS is a filesystem that allows you to mount HTTP directories locally
using FUSE. This document provides a detailed explanation of all the
configuration and usage flags supported by HTTPDirFS.

### Command Syntax

```bash
usage: ./httpdirfs [options] <mountpoint>

FUSE options:
    -h   --help            print help
    -V   --version         print version
    -d   -o debug          enable debug output (implies -f)
    -f                     foreground operation
    -s                     disable multi-threaded operation
    -o clone_fd            use separate fuse device fd for each thread
                           (may improve performance)
    -o max_idle_threads    the maximum number of idle worker threads
                           allowed (default: -1)
    -o max_threads         the maximum number of worker threads
                           allowed (default: 10)
    -o kernel_cache        cache files in kernel
    -o [no]auto_cache      enable caching based on modification times (off)
    -o no_rofd_flush       disable flushing of read-only fd on close (off)
    -o umask=M             set file permissions (octal)
    -o fmask=M             set file permissions (octal)
    -o dmask=M             set dir  permissions (octal)
    -o uid=N               set file owner
    -o gid=N               set file group
    -o entry_timeout=T     cache timeout for names (1.0s)
    -o negative_timeout=T  cache timeout for deleted names (0.0s)
    -o attr_timeout=T      cache timeout for attributes (1.0s)
    -o ac_attr_timeout=T   auto cache timeout for attributes (attr_timeout)
    -o noforget            never forget cached inodes
    -o remember=T          remember cached inodes for T seconds (0s)
    -o modules=M1[:M2...]  names of modules to push onto filesystem stack
    -o allow_other         allow access by all users
    -o allow_root          allow access by root
    -o auto_unmount        auto unmount on process termination

Options for subdir module:
    -o subdir=DIR           prepend this directory to all paths (mandatory)
    -o [no]rellinks         transform absolute symlinks to relative

Options for iconv module:
    -o from_code=CHARSET   original encoding of file names (default: UTF-8)
    -o to_code=CHARSET     new encoding of the file names (default: UTF-8)

general options:
        --config            Specify a configuration file
    -o opt,[opt...]         Mount options
    -h  --help              Print help
    -V  --version           Print version
    -f                      Foreground operation
    -s                      Disable multi-threaded operation
    -d  --debug             Enable debug output (implies -f)

HTTPDirFS options:
    -u  --username          HTTP authentication username
    -p  --password          HTTP authentication password
    -P  --proxy             Proxy for libcurl, for more details refer to
                            https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html
        --proxy-username    Username for the proxy
        --proxy-password    Password for the proxy
        --proxy-cacert      Certificate authority for the proxy
        --proxy-capath      Certificate authority directory for the proxy
        --cache             Enable cache (default: off)
        --cache-location    Set a custom cache location root; each server origin
                            gets its own subdirectory beneath it (default:
                            "${XDG_CACHE_HOME}/httpdirfs")
        --cache-clear       Delete the entire cache location (all server
                            origins), or the custom location specified with
                            --cache-location if that option is given (the order
                            of the two options does not matter). Then exit.
        --cache-clear-host  Delete only the cache of a single server host,
                            given as a full URL or a bare host (both the http
                            and https origin directories are then removed).
                            Then exit.
         --cache-min-size    Set minimum file size threshold for caching, in
                             bytes (K/M/G suffix supported, default: none)
         --cache-max-size    Set maximum file size threshold for caching, in
                             bytes (K/M/G suffix supported, default: none)
        --cacert            Certificate authority for the server
        --capath            Certificate authority directory for the server
         --dl-seg-size       Set cache download segment size, in bytes
                             (K/M/G suffix supported, default: 8M)
                             Note: this setting is ignored if previously
                             cached data is found for the requested file.
        --http-header       Set one or more HTTP headers
        --max-conns         Set maximum number of network connections that
                            libcurl is allowed to make. (default: 6)
        --refresh-timeout   Directory listings and files without verifiable
                            remote metadata are refreshed after the specified
                            time, in seconds (default: 3600)
        --retry-wait        Set delay in seconds before retrying an HTTP request
                            after encountering an error. (default: 5)
        --invalid-refresh   Try refreshing invalid links when reading a directory.
        --user-agent        Set user agent string (default: "HTTPDirFS-1.3.3") # x-release-please-version
        --no-range-check    Disable the built-in check for the server's support
                            for HTTP range requests
        --zero-len-is-dir   If a file has a zero length, treat it as a directory
        --insecure-tls      Disable libcurl TLS certificate verification by
                            setting CURLOPT_SSL_VERIFYHOST to 0
        --allow-external-origin
                            Allow traversing links pointing to external
                            servers (default: off)
        --ignore-anchors    Ignore intra-page HTML anchor/fragment links
                            starting with '#' (default: off)
        --html-is-directory Promote resources with Content-Type text/html to
                            directories (default: off)
        --max-html-size     Set maximum HTML size for directory listing
                            promotion (default: 2M)
        --single-file-mode  Single file mode - rather than mounting a whole
                            directory, present a single file inside a virtual
                            directory.

    For mounting a Airsonic / Subsonic server:
        --sonic-username    The username for your Airsonic / Subsonic server
        --sonic-password    The password for your Airsonic / Subsonic server
        --sonic-id3         Enable ID3 mode - this present the server content in
                            Artist/Album/Song layout
        --sonic-insecure    Authenticate against your Airsonic / Subsonic server
                            using the insecure username / hex encoded password
                            scheme

```

______________________________________________________________________

#### `--html-is-directory`

- **Description:** By default, resources whose URLs do not end with a trailing
  slash (`/`) are treated as regular files. When `--html-is-directory` is
  enabled, HTTPDirFS inspects the HTTP `Content-Type` response header of linked
  resources during link initialization. Any resource returning
  `Content-Type: text/html` (with a size within `--max-html-size`) is promoted
  to a virtual directory, allowing you to browse into it as a subdirectory.
  Non-HTML resources remain regular files.

#### `--max-html-size <size>`

- **Description:** Sets the maximum size of an HTML document eligible for
  directory listing promotion (default: `2M`). HTML resources exceeding this
  threshold are presented as regular files instead of directories to prevent
  excessive memory usage. Suffixes like `K`, `M`, or `G` are supported (e.g.,
  `--max-html-size 4M`).

For the comprehensive architectural specification, see
[docs/specs/directory_detection_and_naming.md](docs/specs/directory_detection_and_naming.md).

______________________________________________________________________

### External Origins (`--allow-external-origin`)

By default, HTTPDirFS confines filesystem traversal strictly to the origin
(scheme, host, port) of the URL specified at mount time. Any links pointing to
external (cross-origin) servers are dropped during HTML parsing.

Enabling `--allow-external-origin` allows HTTPDirFS to follow and mount links
pointing to external origins.

#### How It Works

- **File and Directory Exposure:** External files and directories will appear
  alongside local files in the mountpoint. External URLs ending with a trailing
  slash (`/`) are treated as directories; navigating into them triggers
  recursive discovery on the remote server.
- **Cache Compatibility:** Caching works seamlessly with external links. Cache
  entries are keyed by the full canonical URL, so external-origin content is
  safely hashed into the mounted origin's own cache directory (beneath the cache
  location root), with no risk of path traversal or cross-origin collisions.

#### Security & Credentials Scoping

- **Credential Protection:** To prevent credential leakage, HTTP credentials
  specified with `-u`/`--username` and `-p`/`--password` are strictly scoped to
  the primary mounted origin. They are **not** forwarded to cross-origin
  servers.
- **Custom HTTP Headers:** Custom HTTP headers set via `--http-header` are also
  strictly scoped to the primary origin and will not be sent to external hosts.
- **Authentication Warnings:** External servers requiring authentication
  (returning HTTP 401 or 403) will log a warning indicating that credentials are
  restricted to the main server.

______________________________________________________________________

### Airsonic / Subsonic Mounting Options

HTTPDirFS can mount remote Airsonic or Subsonic music collections. When mounted,
the music structure is presented locally as virtual directories and files.

You simply have to supply both `--sonic-username` and `--sonic-password` to
trigger the \*sonic server mode. For example:

```
./httpdirfs -f --cache --sonic-username $USERNAME --sonic-password $PASSWORD $URL $MOUNT_POINT
```

You definitely want to enable the cache for this one, otherwise it is painfully
slow.

There are two ways of mounting your \*sonic server

- the index mode
- and the ID3 mode.

In the index mode, the filesystem is presented based on the listing on the
`Index` link in your \*sonic's home page.

In ID3 mode, the filesystem is presented using the following hierarchy: 0. Root

1. Alphabetical indices of the artists' names
1. The artists' names
1. All of the albums by a single artist
1. All the songs in an album.

By default, \*sonic server is mounted in the index mode. If you want to mount in
ID3 mode, please use the `--sonic-id3` flag.

Please note that the cache feature is unaffected by how you mount your \*sonic
server. If you mounted your server in index mode, the cache is still valid in
ID3 mode, and vice versa.

HTTPDirFS is also known to work with the following applications, which implement
some or all of Subsonic API:

- [Funkwhale](https://funkwhale.audio/) (requires `--sonic-id3` and
  `--no-range-check`, more information in
  [issue #45](https://github.com/fangfufu/httpdirfs/issues/45))
- [LMS](https://github.com/epoupon/lms) (requires `--sonic-insecure` and
  `--no-range-check`, more information in
  [issue #46](https://github.com/fangfufu/httpdirfs/issues/46). To mount the
  [demo instance](https://lms-demo.poupon.dev/), you might also need
  `--insecure-tls`)
- [Navidrome](https://github.com/navidrome/navidrome), more information in
  [issue #51](https://github.com/fangfufu/httpdirfs/issues/51).

#### `--sonic-username <string>`

- **Description:** The username to authenticate against your Airsonic / Subsonic
  server.

#### `--sonic-password <string>`

- **Description:** The password to authenticate against your Airsonic / Subsonic
  server.

#### `--sonic-id3`

- **Description:** Enables ID3 presentation mode. Instead of representing the
  music collection in a flat or default structure, HTTPDirFS dynamically parses
  metadata and presents the directory layout in an elegant
  `Artist/Album/Song.mp3` layout.

#### `--sonic-insecure`

- **Description:** Forces authentication using the plaintext username and
  hex-encoded password scheme instead of the modern salted MD5 token handshake.
  Useful for legacy servers that do not support modern handshakes.
