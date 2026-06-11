## HTTPDirFS Usage Guide

HTTPDirFS is a filesystem that allows you to mount HTTP directories locally
using FUSE. This document provides a detailed explanation of all the
configuration and usage flags supported by HTTPDirFS.

### Command Syntax

Below is the raw help output displaying all available flags, generated from Git
commit SHA
[`0855c0a`](https://github.com/fangfufu/httpdirfs/commit/0855c0a46a2fa8f8e2d4d084f51d5d124092d067):

```bash
usage: httpdirfs [options] URL mountpoint

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
        --cache-location    Set a custom cache location
                            (default: "${XDG_CACHE_HOME}/httpdirfs")
        --cache-clear       Delete the cache directory or the custom location
                            specified with `--cache-location`, if the option is
                            seen first. Then exit in either case.
        --cache-min-size    Set minimum file size threshold for caching, in bytes
                            (default: none)
        --cache-max-size    Set maximum file size threshold for caching, in bytes
                            (default: none)
        --cacert            Certificate authority for the server
        --capath            Certificate authority directory for the server
        --dl-seg-size       Set cache download segment size, in MB (default: 8)
                            Note: this setting is ignored if previously
                            cached data is found for the requested file.
        --http-header       Set one or more HTTP headers
        --max-conns         Set maximum number of network connections that
                            libcurl is allowed to make. (default: 6)
        --refresh-timeout   The directories are refreshed after the specified
                            time, in seconds (default: 3600)
        --retry-wait        Set delay in seconds before retrying an HTTP request
                            after encountering an error. (default: 5)
        --invalid-refresh   Try refreshing invalid links when reading a directory.
        --user-agent        Set user agent string (default: "HTTPDirFS-1.3.3")
        --no-range-check    Disable the built-in check for the server's support
                            for HTTP range requests
        --zero-len-is-dir   If a file has a zero length, treat it as a directory
        --insecure-tls      Disable libcurl TLS certificate verification by
                            setting CURLOPT_SSL_VERIFYHOST to 0
        --external-links    Include external (cross-origin) links from
                            directory listings (default: off)
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

### General Options

#### `--config <path>`

- **Description:** Specify the path to a configuration file containing option
  key-value pairs. Using a configuration file is a clean alternative to passing
  multiple flags directly via the command line.
- **Example:**
  `httpdirfs --config /etc/httpdirfs/mount.conf http://example.com/dir /mnt/dir`

#### `-o opt,[opt...]`

- **Description:** Pass options directly to the underlying FUSE library or load
  modules. This is extremely powerful for customizing filesystem behavior,
  permission maps, performance tweaks, and character encoding.

  **Commonly Used FUSE Options:**

  - `ro`: Mount the filesystem as read-only.
  - `allow_other`: Allow other users on the system to access the mountpoint. By
    default, only the mounting user has access.
  - `allow_root`: Allow the root user to access the mountpoint.
  - `auto_unmount`: Automatically unmount the filesystem when the mounting
    process terminates.
  - `kernel_cache`: Cache files in the kernel to significantly speed up repeated
    reads.
  - `umask=M`, `fmask=M`, `dmask=M`: Customize permission masks for files and
    directories (specified in octal, e.g., `umask=022`).
  - `uid=N`, `gid=N`: Override the user ID and group ID ownership of all virtual
    files.

  **Commonly Used Module Options:**

  - `subdir=DIR`: Prepend the specified directory `DIR` to all paths (loads the
    subdir module).
  - `rellinks` / `norellinks`: Transform absolute symlinks to relative ones
    within the subdir module.
  - `from_code=CHARSET` / `to_code=CHARSET`: Translate character encodings of
    filenames (e.g., from UTF-8 to ISO-8859-1) (loads the iconv module).

- **Example:**
  `httpdirfs -o allow_other,ro,auto_unmount http://example.com/dir /mnt/dir`

#### `-h, --help`

- **Description:** Prints a concise summary of the command-line usage and exits.

#### `-V, --version`

- **Description:** Prints the current version information of HTTPDirFS and
  exits.

#### `-f`

- **Description:** Runs HTTPDirFS in the foreground. By default, HTTPDirFS
  daemonizes and runs in the background. Running in the foreground is useful for
  debugging or when running inside containers (e.g., Docker).

#### `-s`

- **Description:** Disables multi-threaded operation. This runs FUSE and network
  operations in a single thread, which can be useful for debugging concurrency
  issues.

#### `-d, --debug`

- **Description:** Enables verbose debug output. This output includes detailed
  curl request logs and FUSE kernel-level interaction logs. Note that this flag
  automatically implies foreground operation (`-f`).

______________________________________________________________________

### HTTPDirFS Core Options

#### `-u, --username <string>`

- **Description:** Sets the HTTP authentication username for servers requiring
  Basic, Digest, or NTLM authentication.
- **Note:** Credentials are automatically scoped to the primary mount server
  only.

#### `-p, --password <string>`

- **Description:** Sets the HTTP authentication password for servers requiring
  authentication.

#### `-P, --proxy <url>`

- **Description:** Set the HTTP proxy server URL (e.g., `http://127.0.0.1:8080`)
  for all network requests. For more details, refer to the
  [CURLOPT_PROXY documentation](https://curl.se/libcurl/c/CURLOPT_PROXY.html).

#### `--proxy-username <string>`

- **Description:** The username to authenticate against the specified proxy
  server.

#### `--proxy-password <string>`

- **Description:** The password to authenticate against the specified proxy
  server.

#### `--proxy-cacert <path>`

- **Description:** Path to a custom Certificate Authority (CA) bundle file used
  to verify the TLS certificate of the proxy server.

#### `--proxy-capath <path>`

- **Description:** Path to a directory containing CA certificates to verify the
  TLS certificate of the proxy server.

#### `--cacert <path>`

- **Description:** Path to a Certificate Authority (CA) PEM bundle file to
  verify the SSL certificate of the remote HTTPS directory server.

#### `--capath <path>`

- **Description:** Path to a directory containing CA PEM certificates to verify
  the SSL certificate of the HTTPS directory server.

#### `--insecure-tls`

- **Description:** Disables TLS certificate validation of the HTTPS server (sets
  `CURLOPT_SSL_VERIFYPEER` and `CURLOPT_SSL_VERIFYHOST` to 0). Useful for
  mounting servers using self-signed certificates in private networks.
- **Warning:** Disabling certificate verification exposes connections to
  potential man-in-the-middle (MitM) attacks.

______________________________________________________________________

### Cache Settings

#### `--cache`

- **Description:** Enables the persistent local cache mode. When cache mode is
  active, HTTPDirFS caches directory trees and file content segments locally on
  disk. Subsequent directory reads and file accesses are served from the cache,
  significantly improving speed and reducing network usage.

#### `--cache-location <path>`

- **Description:** Specify a custom directory path where cache data and metadata
  should be stored.
- **Default:** `${XDG_CACHE_HOME}/httpdirfs` (usually resolves to
  `~/.cache/httpdirfs`).

#### `--cache-clear`

- **Description:** Deletes the existing cache directory (or the custom cache
  path if `--cache-location` is specified before this option) and immediately
  exits. Highly useful for cleaning up disk space or forcing a full directory
  recrawl.

#### `--dl-seg-size <size>`

- **Description:** Sets the size of individual file cache segments in Megabytes
  (MB).
- **Default:** `8`
- **Note:** This setting is ignored for files that already have existing cached
  segment data on disk.

#### `--cache-min-size <bytes>`

- **Description:** Sets the minimum file size threshold for caching in bytes.
  Files with a size smaller than this threshold will bypass the local disk cache
  and be downloaded directly from the network upon access.
- **Default:** None (no minimum limit is set).

#### `--cache-max-size <bytes>`

- **Description:** Sets the maximum file size threshold for caching in bytes.
  Files with a size larger than this threshold will bypass the local disk cache
  and be downloaded directly from the network upon access.
- **Default:** None (no maximum limit is set).

______________________________________________________________________

### Network & Performance Options

#### `--http-header <header>`

- **Description:** Set one or more custom HTTP headers for requests sent to the
  mounted server. This option can be specified multiple times.
- **Example:**
  `httpdirfs --http-header "Authorization: Bearer token" --http-header "X-Custom: value" URL mountpoint`

#### `--max-conns <count>`

- **Description:** Sets the maximum number of concurrent network connections
  libcurl is allowed to establish.
- **Default:** `6`
- **Tip:** Lowering this number reduces load on remote servers and helps prevent
  rate-limiting or blocking.

#### `--refresh-timeout <seconds>`

- **Description:** Sets the duration in seconds after which directory listings
  are treated as stale and are refetched from the remote server when accessed.
- **Default:** `3600` (1 hour)

#### `--retry-wait <seconds>`

- **Description:** Sets the delay in seconds to wait before retrying an HTTP
  request after a connection failure or server error.
- **Default:** `5`

#### `--user-agent <string>`

- **Description:** Customizes the HTTP `User-Agent` header sent with each
  request.
- **Default:** `HTTPDirFS-1.3.3`

#### `--no-range-check`

- **Description:** Skips the built-in HTTP Range check. Normally, HTTPDirFS
  tests whether the remote server supports HTTP Range requests (required for
  random-access file seeks). If you know your server supports Range requests but
  fails the probe, enable this flag.
- **Warning:** If the remote server truly does not support Range requests,
  reading files from the mountpoint will be extremely slow or fail.

______________________________________________________________________

### Behavioral & Advanced Flags

#### `--invalid-refresh`

- **Description:** Tries to refresh and resolve invalid or expired links
  dynamically when reading directory contents, rather than relying strictly on
  the refresh timeout.

#### `--zero-len-is-dir`

- **Description:** Instructs HTTPDirFS to treat any file listed with a length of
  `0` bytes as a subdirectory. Useful for servers that do not end directory URLs
  with `/` in their listings but represent them with a size of zero.

#### `--single-file-mode`

- **Description:** Launches HTTPDirFS in Single File Mode. Rather than
  attempting to mount and traverse a directory listing, it mounts the specified
  URL as a single virtual file inside the mountpoint. This is highly useful for
  files hosted on servers that do not present any directory listings.

______________________________________________________________________

### External Links (`--external-links`)

When `--external-links` is enabled, HTTPDirFS parses the HTML directory listing
of the mounted server and identifies any `<a href>` tags pointing to absolute
external (cross-origin) URLs.

#### How It Works

- **File and Directory Exposure:** External files and directories will appear
  alongside local files in the mountpoint. External URLs ending with a trailing
  slash (`/`) are treated as directories; navigating into them triggers a
  recursive layout discovery on the remote server.
- **First-Wins Deduplication:** If multiple external links produce the same
  filename after extraction, only the first encountered link is kept.
- **Path Sanitization:** Filenames derived from external URLs are automatically
  sanitized. URL-decoded slash characters (`%2F`) are unescaped and converted to
  underscores (`_`) to prevent directory traversal or invalid FUSE entry
  creations.
- **Cache Compatibility:** Caching works seamlessly with external links. Cache
  paths for external files are safely flattened and sanitized within the
  metadata and data cache directory structures to avoid directory traversal.

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
