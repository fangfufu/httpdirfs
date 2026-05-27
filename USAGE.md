## HTTPDirFS Usage

As of commit
[666d53ae281a5a5a71f9fed2e5483e6f7dac812f](https://github.com/fangfufu/httpdirfs/commit/666d53ae281a5a5a71f9fed2e5483e6f7dac812f),
HTTPDirFS supports the following usage flags:

    usage: ./httpdirfs [options] URL mountpoint

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
            --user-agent        Set user agent string (default: "HTTPDirFS-VERSION")
            --no-range-check    Disable the built-in check for the server's support
                                for HTTP range requests
            --zero-len-is-dir   If a file has a zero length, treat it as a directory
            --insecure-tls      Disable licurl TLS certificate verification by
                                setting CURLOPT_SSL_VERIFYHOST to 0
            --external-links    Include external (cross-origin) links from
                                directory listings. When enabled, httpdirfs will
                                follow <a href> tags pointing to other servers
                                and expose their files alongside local links.
                                HTTP credentials (-u/-p) are only applied to
                                the mounted server; external servers requiring
                                authentication will generate a warning.
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
