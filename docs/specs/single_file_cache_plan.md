# Unified Single-File Cache Architecture Plan

This document outlines the design and implementation plan for HTTPDirFS's
unified single-file cache architecture, extended with first-class HTTP `HEAD`
response and file stat caching.

______________________________________________________________________

## 1. Architectural Principles

1. **One URL = Exactly One File on Disk**:

   - Every cached URL (whether directory HTML, file data, or `HEAD` file stat)
     is stored in a single container file at
     `<cache_root>/<escaped_origin>/ab/<hash>`.
   - Eliminates auxiliary files per directory (`.LinkTable`,
     `.httpdirfs_content`, `.httpdirfs_header`).
   - Eliminates auxiliary files per file (`.meta` vs `.data`).

1. **First-Class HEAD & File Stat Caching**:

   - HTTP `HEAD` responses (status code, Content-Length, Last-Modified/filetime,
     and Content-Type) are cached in the container file before file payload is
     ever downloaded.
   - When directories are re-opened (or when links are shared across multiple
     directory pages), file stats are resolved instantly from disk (< 0.05 ms)
     without sending redundant network requests.
   - Seamlessly promoted: when payload data is subsequently downloaded, the
     existing container file is expanded with the payload without altering its
     metadata or hash location.

1. **Abandon On-Disk LinkTable Serialization**:

   - Stop serializing in-memory C structs (`LinkTable`) to disk.
   - Cache the raw HTTP response (response headers + HTML payload).
   - Regenerate the `LinkTable` in-memory on-the-fly via
     `LinkTable_parse_html()` using the Gumbo HTML parser (< 0.5 ms).

1. **Unified Content Model**:

   - In HTTP, an HTML directory listing is simply an HTTP resource
     (`text/html`), just like a video or binary file is an HTTP resource
     (`application/...`).
   - The cache engine treats all resources uniformly as raw HTTP content.
   - If a URL is cached as a file and later opened as a directory, HTTPDirFS
     simply re-parses the existing cached payload in memory with zero
     byte-shifting or file conversion.

1. **1-Level Hash Sharding (`<origin>/ab/<hash>`)**:

   - Shards files into 256 directories (`00` to `ff`) using the first 2 hex
     characters of the MD5 hash.
   - Completely eliminates file-versus-directory path collisions.
   - Immune to Linux `NAME_MAX` (255 bytes) and `PATH_MAX` (4096 bytes) limits.

1. **Deterministic Timestamp Invalidation**:

   - Explicitly records both the local download timestamp (`cache_time`) and the
     upstream server's `Last-Modified` timestamp (`remote_mtime`) in the binary
     header.
   - Cache freshness is evaluated as
     `time(NULL) - cache_time <= CONFIG.refresh_timeout`.

1. **Deterministic URL Canonicalization**:

   - URLs are normalized via `canonicalize_url()` prior to MD5 hashing and cache
     path derivation.
   - Normalization rules:
     - Scheme and host are converted to lowercase.
     - Default ports are removed (`:80` for HTTP, `:443` for HTTPS).
     - URL fragments (`#...`) are stripped because fragments are client-only and
       never sent over HTTP.
     - Redundant slashes and dot-segments (`.` and `..`) are resolved in the
       path while preserving trailing slashes.
     - Percent-encoding is normalized per RFC 3986 (uppercase hex digits `%2F`,
       unreserved characters decoded).
   - Guarantees that semantically identical URLs (e.g.
     `HTTP://Example.com:80/dir#ref` and `http://example.com/dir`) resolve to
     the exact same cache container.

1. **Transparent HTTP Redirect Aliasing (Pointer Containers)**:

   - When a requested URL redirects (HTTP 301, 302, 307, 308) or when curl's
     `CURLINFO_EFFECTIVE_URL` differs from the request URL (such as directory
     trailing slash redirection), HTTPDirFS stores:
     1. The full payload / stat container under the canonical target URL.
     1. A lightweight Redirect Pointer Container (`CACHE_FLAG_IS_REDIRECT`)
        under the requested source URL, containing the canonical target URL
        string.
   - When reading any cache container, redirect pointers are transparently
     followed to the target URL container with a loop guard (maximum depth of 5
     hops).
   - Prevents duplicate downloads and eliminates network requests when links
     query the unredirected form of a URL.

1. **No Backward Compatibility Required**:

   - Legacy cache formats (`.LinkTable`, `.meta`, `.data`) are completely
     discarded; existing cache directories can be freshly populated with the
     single-file container format.

______________________________________________________________________

## 2. On-Disk Layout & Binary Specification

### 2.1 Directory Sharding Structure

Under the cache root directory (default `~/.cache/httpdirfs/` or custom
`--cache-location <dir>`):

```
~/.cache/httpdirfs/
├── CACHEDIR.TAG
└── https%3A%2F%2Fexample.com/       <-- Escaped server origin (scheme://host[:port])
    ├── 00/
    ├── 01/
    ├── ...
    ├── a3/
    │   └── a3f58b09c123456789abcdef01234567  <-- Unified container file
    └── ff/
```

- **Origin Directory**: Extracted using `get_server_root(url)` and escaped using
  `curl_easy_escape()`.
- **Shard Directory (`ab/`)**: Characters `0..1` of the 32-character hexadecimal
  MD5 hash (`generate_md5sum(canonical_url)`).
- **Filename (`<hash>`)**: The full 32-character MD5 hash string.

______________________________________________________________________

### 2.2 Unified Container File Format

Every cached object on disk begins with a 64-byte `CacheHeader`:

```
+-------------------------------------------------------------------------+
| CacheHeader (Fixed size: 64 bytes)                                      |
|   - magic (uint32_t): 0x53464448 ("HDFS" in little-endian)              |
|   - version (uint16_t): 1                                               |
|   - flags (uint16_t): Bitmask (see below)                               |
|   - url_len (uint32_t): Length of canonical source URL string           |
|   - header_size (uint32_t): Offset where payload begins (4KB aligned)   |
|   - http_header_len (uint32_t): Length of raw HTTP response headers     |
|   - cache_time (int64_t): Local time(NULL) when downloaded              |
|   - remote_mtime (int64_t): Remote server Last-Modified timestamp (or 0)|
|   - content_length (int64_t): Total length of payload in bytes          |
|   - blksz (int32_t): Segment block size (e.g. 8 MB)                     |
|   - segbc (int32_t): Total segment count (1 for HTML, 0 for HEAD-only)  |
|   - reserved (uint8_t[12]): Reserved for future use (zeroed)            |
+-------------------------------------------------------------------------+
| Canonical Source URL String (url_len bytes, null-terminated)            |
+-------------------------------------------------------------------------+
| Raw HTTP Response Headers (http_header_len bytes: status line + headers)|
+-------------------------------------------------------------------------+
| Segment Bitmap (segbc bytes: uint8_t seg[] array, omitted if segbc==0)  |
+-------------------------------------------------------------------------+
| 4 KB Page Alignment Padding (zero bytes to align next section)          |
+-------------------------------------------------------------------------+ <-- header_size (offset 4096)
| Payload Data (HTML text OR file binary, content_length bytes)           |
| (Omitted on disk if CACHE_FLAG_IS_HEAD is set and payload not downloaded)|
+-------------------------------------------------------------------------+
```

#### Bitmask Flags

```c
#define CACHE_FLAG_IS_COMPLETE 0x1  /* Payload is fully downloaded */
#define CACHE_FLAG_IS_SPARSE   0x2  /* File payload was sparse-allocated */
#define CACHE_FLAG_IS_HEAD     0x4  /* Container contains cached HEAD / stat metadata */
#define CACHE_FLAG_IS_DIR      0x8  /* Classified as a directory (text/html) */
#define CACHE_FLAG_IS_REDIRECT 0x10 /* Container is a redirect pointer to target URL */
```

#### Container States

1. **HEAD-Only Container (`CACHE_FLAG_IS_HEAD`)**:
   - `content_length`: Remote file size from `Content-Length:` header (or 0 for
     directory).
   - `remote_mtime`: Remote file timestamp from `Last-Modified:` /
     `CURLINFO_FILETIME`.
   - `http_header_len`: Contains the raw status line (e.g. `HTTP/1.1 200 OK`)
     and HTTP headers (including `Content-Type:`).
   - `segbc`: 0.
   - `header_size`: 4096.
   - **On-disk file size**: `sizeof(CacheHeader) + url_len + http_header_len`
     (typically 300–600 bytes, no payload allocated).
1. **Directory Listing Container
   (`CACHE_FLAG_IS_COMPLETE | CACHE_FLAG_IS_DIR`)**:
   - Contains raw HTTP headers and full HTML payload.
   - `segbc`: 1.
1. **Partial File Container (`CACHE_FLAG_IS_SPARSE`)**:
   - Page-aligned payload sparse-allocated with `ftruncate()`, segment bitmap
     tracks downloaded blocks.
1. **Complete File Container (`CACHE_FLAG_IS_COMPLETE`)**:
   - Full file data downloaded and verified.
1. **Redirect Pointer Container (`CACHE_FLAG_IS_REDIRECT`)**:
   - `http_resp`: Upstream HTTP redirect status code (e.g. 301, 302, 307, 308).
   - `url_len`: Length of the requested source URL string.
   - `content_length`: Length of the canonical target URL string.
   - `http_header_len`: Raw HTTP redirect response headers (e.g. `Location:`).
   - `segbc`: 0.
   - `header_size`: Offset where target URL is stored.
   - **Payload section**: The null-terminated canonical target URL string.
   - **On-disk file size**:
     `sizeof(CacheHeader) + url_len + http_header_len + content_length`
     (typically < 256 bytes, tiny pointer).

______________________________________________________________________

## 3. Operations & Code Integration

### 3.1 URL Canonicalization (`src/url.c`, `src/url.h`)

All cache path lookups, container creations, and header writes pass URLs through
`canonicalize_url()` before hashing.

```c
/**
 * \brief Canonicalize a URL for deterministic cache hashing and matching.
 * \param url The raw URL string.
 * \return Heap-allocated canonical URL string, or NULL on error.
 */
char *canonicalize_url(const char *url);
```

#### Canonicalization Rules:

1. **Fragment Removal**: URL fragments (`#section`) are client-side only and
   never sent to upstream HTTP servers. Fragments are stripped completely.
1. **Case Normalization**: Scheme and hostname are converted to lowercase
   (`HTTP://EXAMPLE.COM` -> `http://example.com`).
1. **Default Port Removal**: Standard default scheme ports are stripped (`:80`
   for `http://`, `:443` for `https://`). Non-standard ports (`:8080`, `:8443`)
   are strictly preserved.
1. **Path Dot-Segment & Redundant Slash Normalization**:
   - Resolves relative segments (`.` and `..`) in the path per RFC 3986 Section
     5.2.4.
   - Collapses consecutive duplicate slashes (`//` -> `/`).
   - Strictly preserves trailing slashes (e.g. `/dir/` vs `/dir`) because
     trailing slashes signify directory vs file semantics in HTTP.
1. **Percent-Encoding Normalization**:
   - Decodes unreserved characters per RFC 3986 (`[A-Za-z0-9-_.~]`).
   - Converts percent-encoded hexadecimal digits to uppercase (`%2f` -> `%2F`).
1. **Implementation via libcurl URL API**:
   - Utilizes `curl_url()`, `curl_url_set()`, and `curl_url_get()` with
     `CURLU_PATH_AS_IS` and `CURLU_DEFAULT_SCHEME`, ensuring rock-solid RFC 3986
     compliance.

### 3.2 Path Derivation (`src/url.c`, `src/url.h`)

- `url_to_cache_path()` normalizes the input URL before generating the MD5 hash:
  ```c
  char *url_to_cache_path(const char *url)
  {
      if (!url) return NULL;
      char *canonical_url = canonicalize_url(url);
      const char *target = canonical_url ? canonical_url : url;
      char *hash = generate_md5sum(target);
      char rel_path[PATH_MAX];
      snprintf(rel_path, sizeof(rel_path), "%.2s/%s", hash, hash);
      FREE(hash);
      FREE(canonical_url);
      return STRDUP(rel_path);
  }
  ```

### 3.3 HEAD & Stat Caching API (`src/cache.h`, `src/cache.c`)

```c
typedef struct CacheStat {
    long http_resp;           /**< HTTP status code (e.g. 200) */
    curl_off_t content_length;/**< Remote content length */
    time_t remote_mtime;      /**< Last-Modified timestamp */
    char content_type[128];   /**< Content-Type MIME string */
    LinkType link_type;       /**< LINK_DIR, LINK_FILE, or LINK_INVALID */
} CacheStat;

/**
 * \brief Write an HTTP HEAD response to the URL's container file.
 * \return 0 on success, -1 on error
 */
int CacheContainer_write_head(const char *url, long http_resp,
                             curl_off_t content_length, time_t remote_mtime,
                             const char *content_type, const char *raw_headers,
                             size_t raw_headers_len, LinkType link_type);

/**
 * \brief Read cached HTTP HEAD / stat metadata for a URL if fresh.
 * \return 1 if found and fresh, 0 if not cached or expired, -1 on corrupt
 */
int CacheContainer_read_head(const char *url, CacheStat *stat_out);

/**
 * \brief Write a redirect pointer container pointing to target_url.
 * \return 0 on success, -1 on error
 */
int CacheContainer_write_redirect(const char *source_url, const char *target_url,
                                 long http_resp);
```

### 3.4 Directory Listing & Stat Resolution (`src/link.c`)

#### A. Cache-First Stat Resolution in `LinkTable_uninitialised_fill`

Before queuing live network requests:

1. Iterate through `linktbl->links[i]`:
   - If `CACHE_SYSTEM_INIT` is enabled, call
     `CacheContainer_read_head(link->f_url, &stat)`.
   - On cache hit (`1`):
     - `link->time = stat.remote_mtime;`
     - `link->content_length = stat.content_length;`
     - `link->type = stat.link_type;`
   - On cache miss (`0` or `-1`):
     - Keep as `LINK_UNINITIALISED_*` and queue for network
       `Link_req_file_stat()`.
1. If all links hit the cache, `LinkTable_uninitialised_fill()` returns
   immediately (**0 network requests, instantaneous directory display**).

#### B. Cache Write in `filestat_on_complete` (`src/transfer.c`)

When an uncached network `HEAD` request finishes:

1. Capture raw response headers in `TransferStruct`.
1. In `filestat_on_complete()`, after `Link_set_file_stat(link, curl)`
   classifies the link:
1. If `CACHE_SYSTEM_INIT` is enabled:
   - Call `CacheContainer_write_head()` with the resolved `http_resp`,
     `content_length`, `remote_mtime`, `content_type`, `raw_headers`, and
     `link->type`.
1. Subsequent lookups for this URL across any directory or restart will hit the
   cache.

### 3.5 Promotion to File Container on Download (`src/cache.c`)

When `Cache_create()` is called to download file data:

1. If the container file already exists with `CACHE_FLAG_IS_HEAD`:
   - Read the existing `CacheHeader`.
   - Calculate `blksz`, `segbc`, and `header_size = 4096`.
   - Update header flags (`CACHE_FLAG_IS_SPARSE`), append the zeroed segment
     bitmap, pad to 4096 bytes.
   - Call `ftruncate(fd, header_size + content_length)`.
1. Existing metadata is preserved with zero re-downloading of headers.

### 3.6 HTTP Redirect Pointer Handling (`src/cache.c`, `src/transfer.c`)

#### A. Redirect Pointer Creation

When a transfer completes in `filestat_on_complete()` or
`transfer_on_complete()`:

1. Obtain effective URL via
   `curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url)`.
1. Obtain HTTP response code via
   `curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp)`.
1. If `eff_url` differs from `orig_url` (or if `http_resp` is 301, 302, 307, or
   308):
   - Write the full stat or payload container under `canonicalize_url(eff_url)`.
   - Call `CacheContainer_write_redirect(orig_url, eff_url, http_resp)` to write
     the lightweight pointer container under `canonicalize_url(orig_url)`.
   - The pointer container stores `flags = CACHE_FLAG_IS_REDIRECT`,
     `content_length = strlen(eff_url)`, and the payload contains `eff_url`.

#### B. Transparent Pointer Following

When reading a container via `CacheContainer_read_head()`, `Cache_create()`, or
`CacheContainer_read()`:

1. Derive cache path for `url`.
1. Open container header.
1. If `flags & CACHE_FLAG_IS_REDIRECT`:
   - Read the canonical target URL from the container payload.
   - Enforce a maximum redirection depth of 5 to guard against redirect loops.
   - Recurse resolution using the target URL.
1. Callers transparently receive the target resource's metadata or data with
   zero awareness of the redirect indirection.

______________________________________________________________________

## 4. Host-Specific Cache Clearing (`--cache-clear-host`)

### 4.1 CLI Option

- Option: `--cache-clear-host <URL_OR_HOST>`.
- Added to `long_opts` in `src/main.c`.

### 4.2 Resolution & Execution

- If argument contains `://`:
  - Extract server root via `get_server_root(arg)`.
  - Escape via `curl_easy_escape()`.
  - Target directory: `<cache_root>/<escaped_server_root>/`.
- If argument has no scheme (e.g. `example.com`):
  - Check for both `https%3A%2F%2F<host>` and `http%3A%2F%2F<host>`.
- Remove directory recursively via `nftw()` and exit with `EXIT_SUCCESS`.

______________________________________________________________________

## 5. Testing Plan

*Strict adherence to user constraint: All tests use synthetic mock URLs
(`https://example.com/test-item`, `https://example.com/test-item/child.bin`).*

1. **Unit Tests (`tests/test_cache.c`, `tests/test_link.c`)**:
   - `test_url_canonicalization`: Verify scheme/host lowercasing, default port
     removal (`:80`, `:443`), fragment stripping (`#fragment`), path
     normalization (`.` and `..`), and deterministic hash equality between
     unnormalized and normalized URL strings.
   - `test_cache_path_derivation`: Verify `"ab/<hash>"` format from
     `url_to_cache_path()`.
   - `test_container_head_write_read`: Write a HEAD-only container, verify
     reading headers, MIME type, and stat classification.
   - `test_container_redirect_pointer`: Write a redirect pointer container from
     `https://example.com/dir` to `https://example.com/dir/`, verify transparent
     resolution in `CacheContainer_read_head()`, and verify loop guard prevents
     recursion on circular redirects (`A -> B -> A`).
   - `test_container_head_to_data_promotion`: Create HEAD-only container,
     promote via `Cache_create()`, download data segments, verify data
     integrity.
   - `test_container_html_parse_on_the_fly`: Cache HTML content, re-read and
     parse `LinkTable` on-the-fly.
   - `test_container_timestamps`: Verify `cache_time` expiration against
     `CONFIG.refresh_timeout`.
   - `test_cache_clear_host`: Verify `--cache-clear-host` removes only the
     target host's cache folder.
1. **Integration Tests (`tests/integration/run_integration_test.sh`)**:
   - Verify `meson test -C builddir` passes completely.
   - Test directory opening with cached HEAD requests verifying zero network
     calls.
   - Run `pre-commit` suite (`clang-format`, `clang-tidy`, `codespell`).
