# Unified Single-File Cache Architecture Specification

This document specifies the technical architecture, on-disk container format,
lifecycle state machine, and data structures of HTTPDirFS's unified single-file
cache subsystem.

______________________________________________________________________

## 1. Architectural Principles

The cache engine is designed around seven core architectural tenets:

1. **One URL = Exactly One File on Disk:**

   - Every cached HTTP resource—whether an HTML directory listing, a file
     payload, a `HEAD` response stat, or an HTTP redirect pointer—is stored in a
     single container file at:
     ```
     <cache_root>/<escaped_origin>/ab/<hash>
     ```
   - Eliminates multi-file coordination overhead and stale file hazards inherent
     in separated metadata and payload schemes (`.meta` vs `.data`).
   - Eliminates auxiliary directory index files (`.LinkTable`,
     `.httpdirfs_content`, `.httpdirfs_header`).

1. **First-Class HEAD & File Stat Caching:**

   - HTTP `HEAD` response headers (HTTP status code, `Content-Length`, remote
     `Last-Modified` timestamp, and `Content-Type`) are persisted in a
     lightweight container file before file payload is ever downloaded.
   - When directories are re-opened or links are shared across multiple pages,
     file stats resolve from local disk (< 0.05 ms) without issuing network
     round trips.
   - Progressive promotion: when payload bytes are subsequently requested, the
     existing container file is promoted in place to a data container without
     altering its metadata, path, or hash identity.

1. **In-Memory Dynamic LinkTable Materialization:**

   - On-disk serialization of in-memory C structs (`LinkTable`) is abandoned.
   - Directory listings are stored in their raw HTTP form (response headers and
     HTML payload).
   - In-memory `LinkTable` objects are regenerated on-the-fly via
     `LinkTable_parse_html()` using the Gumbo HTML5 parser (< 0.5 ms),
     eliminating structural versioning incompatibilities.

1. **Unified Content Model:**

   - In HTTP, directory listings (`text/html`) and files (`application/...`,
     `video/...`, etc.) are all standard HTTP resources.
   - The cache engine treats all resources uniformly. If a URL is initially
     cached as a file and subsequently accessed as a directory (e.g., via
     `--website-mode`), HTTPDirFS parses the existing cached payload directly in
     memory with zero file conversion or data migration.

1. **Single-Level Hash Sharding (`<origin>/ab/<hash>`):**

   - Objects are sharded into 256 subdirectories (`00` through `ff`) using the
     first two hexadecimal characters of the 32-character MD5 hash of the
     canonical URL.
   - Completely eliminates file-versus-directory path collision hazards on local
     filesystems.
   - Immune to Linux filename (`NAME_MAX = 255`) and path length
     (`PATH_MAX = 4096`) restrictions.

1. **Deterministic Timestamp Invalidation:**

   - Each container records both the local capture timestamp (`cache_time`) and
     the remote server's `Last-Modified` timestamp (`remote_mtime`) in its
     binary header.
   - Freshness is deterministically evaluated against `CONFIG.refresh_timeout`:
     $$\\text{is_fresh} = (\\text{time}(\\text{NULL}) - \\text{cache_time} \\le
     \\text{CONFIG.refresh_timeout})$$
   - File payload containers are additionally re-validated at open time
     (`Container_read` in `src/cache.c`): they are rejected if the cached
     `remote_mtime` or `content_length` no longer matches the live file stat.

1. **Deterministic URL Canonicalization:**

   - Every URL is normalized via `canonicalize_url()` before hashing and cache
     path resolution.
   - Guarantees that semantically equivalent URLs resolve to the exact same
     cache container.

1. **Transparent HTTP Redirect Aliasing (Pointer Containers):**

   - When a requested URL redirects (HTTP 301, 302, 307, 308) or when the
     effective URL diverges from the request URL, HTTPDirFS persists:
     1. The canonical target container under `canonicalize_url(target_url)`.
     1. A lightweight Redirect Pointer Container (`CACHE_FLAG_IS_REDIRECT`)
        under `canonicalize_url(source_url)`.
   - Pointer containers are followed transparently with an iteration depth guard
     to prevent infinite redirect loops.

______________________________________________________________________

## 2. On-Disk Layout & Binary Specification

### 2.1 Directory Sharding Structure

Under the cache root directory (default `${XDG_CACHE_HOME}/httpdirfs` or the
path specified by `--cache-location`):

```
<cache_root>/
├── CACHEDIR.TAG
└── https%3A%2F%2Fexample.com/       <-- Escaped server origin (scheme://host[:port])
    ├── 00/
    ├── 01/
    ├── ...
    ├── a3/
    │   └── a3f58b09c123456789abcdef01234567  <-- Unified container file
    └── ff/
```

- **Origin Directory**: Extracted via `get_server_root(url)` and escaped using
  `curl_easy_escape()`.
- **Shard Directory (`ab/`)**: Characters `0..1` of the 32-character hexadecimal
  MD5 hash (`generate_md5sum(canonical_url)`).
- **Container Filename (`<hash>`)**: The complete 32-character MD5 hash string.
- **Custom location:** When `--cache-location <dir>` is supplied, `<dir>` is
  used as the cache location root; the escaped origin directory (and a
  `CACHEDIR.TAG`) is created inside it exactly as with the default location.

______________________________________________________________________

### 2.2 Unified Container File Format

Every container file begins with a fixed 64-byte binary `CacheHeader`, followed
by variable-length metadata sections and page-aligned payload data:

```
+-------------------------------------------------------------------------+
| CacheHeader (Fixed size: 64 bytes)                                      |
|   - magic (uint32_t): 0x53464448 ("HDFS" in little-endian)              |
|   - version (uint16_t): 1                                               |
|   - flags (uint16_t): Bitmask (see Section 2.3)                         |
|   - url_len (uint32_t): Byte length of canonical source URL string      |
|   - header_size (uint32_t): Byte offset where payload data begins       |
|   - http_header_len (uint32_t): Byte length of raw HTTP response header |
|   - cache_time (int64_t): Local time(NULL) when container was written   |
|   - remote_mtime (int64_t): Remote Last-Modified timestamp (0 if none)  |
|   - content_length (int64_t): Total length of payload in bytes          |
|   - blksz (int32_t): Segment block size in bytes (e.g. 8 MiB)           |
|   - segbc (int32_t): Total segment count (1 for HTML, 0 for HEAD-only)  |
|   - reserved (uint8_t[12]): Reserved for future extensions (all zero)   |
+-------------------------------------------------------------------------+
| Canonical Source URL String (url_len bytes + null terminator)          |
+-------------------------------------------------------------------------+
| Raw HTTP Response Headers (http_header_len bytes: status line + headers)|
+-------------------------------------------------------------------------+
| Segment Bitmap (segbc bytes: uint8_t seg[] array, omitted if segbc==0)  |
+-------------------------------------------------------------------------+
| 4 KiB Page Alignment Padding (zero bytes to align next section)          |
+-------------------------------------------------------------------------+ <-- header_size (offset 4096)
| Payload Data (HTML text OR file binary, content_length bytes)           |
| (Omitted on disk if CACHE_FLAG_IS_HEAD is set and payload not downloaded)|
+-------------------------------------------------------------------------+
```

#### Field Specifications

| Field             | Type       | Size (Bytes) | Description                                                                 |
| ----------------- | ---------- | ------------ | --------------------------------------------------------------------------- |
| `magic`           | `uint32_t` | 4            | Magic signature: `0x53464448` (ASCII `"HDFS"`, LE)                          |
| `version`         | `uint16_t` | 2            | Container format version (currently `1`)                                    |
| `flags`           | `uint16_t` | 2            | Bitmask defining container state and type                                   |
| `url_len`         | `uint32_t` | 4            | Length of stored canonical URL (null terminator is written but not counted) |
| `header_size`     | `uint32_t` | 4            | Byte offset where payload data begins (4096-byte aligned)                   |
| `http_header_len` | `uint32_t` | 4            | Byte length of raw HTTP response headers                                    |
| `cache_time`      | `int64_t`  | 8            | Local POSIX timestamp when container was created/refreshed                  |
| `remote_mtime`    | `int64_t`  | 8            | Upstream `Last-Modified` timestamp (POSIX seconds, or `0`)                  |
| `content_length`  | `int64_t`  | 8            | Total payload size in bytes (from `Content-Length`)                         |
| `blksz`           | `int32_t`  | 4            | Download segment block size (default: 8 MiB)                                |
| `segbc`           | `int32_t`  | 4            | Segment count in bitmap: $\\lceil \\text{cl} / \\text{blksz}                |
| `reserved`        | `uint8_t`  | 12           | Reserved for future use (must be set to zero)                               |

______________________________________________________________________

### 2.3 Bitmask Flags

```c
#define CACHE_FLAG_IS_COMPLETE 0x1  /* Payload is fully downloaded and verified */
#define CACHE_FLAG_IS_SPARSE   0x2  /* File payload is sparsely allocated on disk */
#define CACHE_FLAG_IS_HEAD     0x4  /* Container contains cached HEAD / file stat */
#define CACHE_FLAG_IS_DIR      0x8  /* Resource represents an HTML directory listing */
#define CACHE_FLAG_IS_REDIRECT 0x10 /* Container is an HTTP redirect pointer */
```

______________________________________________________________________

### 2.4 Container Archetypes

Depending on its flags, a container file exists in one of five distinct states:

```
                      ┌────────────────────────┐
                      │    HTTP HEAD Probe     │
                      └───────────┬────────────┘
                                  │
                                  ▼
                   ┌──────────────────────────────┐
                   │     HEAD-Only Container      │
                   │    (CACHE_FLAG_IS_HEAD)      │
                   └──────────────┬───────────────┘
                                  │
                  First read / download initiated
                                  │
                                  ▼
                   ┌──────────────────────────────┐
                   │    Sparse File Container     │
                   │    (CACHE_FLAG_IS_SPARSE)    │
                   └──────────────┬───────────────┘
                                  │
                    All bitmap segments complete
                                  │
                                  ▼
                   ┌──────────────────────────────┐
                   │   Complete File Container    │
                   │   (CACHE_FLAG_IS_COMPLETE)   │
                   └──────────────────────────────┘
```

1. **HEAD-Only Container (`CACHE_FLAG_IS_HEAD`):**

   - Created immediately upon completing an HTTP `HEAD` probe.
   - Stores `content_length` (remote file size) and `remote_mtime`.
   - Stores raw HTTP response headers (including `Content-Type`).
   - `segbc = 0`. `header_size` is the 4096-aligned size of the metadata
     sections (typically `4096`). The file itself is **not** padded to
     `header_size`.
   - Written atomically to a temporary file, then renamed into place.
   - **On-disk size:** `64 + url_len + 1 + http_header_len` (typically 300–600
     bytes; no payload bytes allocated).

1. **Directory Listing Container
   (`CACHE_FLAG_IS_SPARSE | CACHE_FLAG_IS_COMPLETE | CACHE_FLAG_IS_DIR`):**

   - Created upon downloading the HTML payload of a directory listing.
   - Stores raw HTTP headers and full HTML text.
   - $\\text{segbc} = \\lceil \\text{payload_len} / \\text{blksz} \\rceil$ (i.e.
     `1` for any payload smaller than the segment block size, which covers all
     listings under the default `--max-html-size`). Every bitmap segment is
     marked present.
   - Payload begins at offset `header_size` (4096-aligned, typically `4096`).

1. **Sparse File Container (`CACHE_FLAG_IS_SPARSE`):**

   - Created when a cached file download begins.
   - The file is extended to `header_size + content_length` using `ftruncate()`.
   - The segment bitmap array (`uint8_t seg[segbc]`) records which blocks are
     present on disk:
     - `seg[i] == 0`: Block not yet downloaded.
     - `seg[i] == 1`: Block downloaded and verified.

1. **Complete File Container (`CACHE_FLAG_IS_COMPLETE`):**

   - Transitioned when all elements of the segment bitmap are marked complete.
   - All file bytes are populated on disk and can be served without network
     involvement.

1. **Redirect Pointer Container (`CACHE_FLAG_IS_REDIRECT`):**

   - Created when upstream responds with HTTP 301, 302, 307, or 308, or when
     redirection alters the effective URL.
   - `url_len`: Byte length of requested source URL.
   - `content_length`: Byte length of canonical target URL string (no null
     terminator).
   - `blksz`: Repurposed to store the HTTP redirect status code (e.g. `301`).
   - Payload section: Contains the canonical target URL, exactly
     `content_length` bytes, starting at offset `header_size`.
   - Written atomically to a temporary file, then renamed into place.
   - **On-disk size:** `header_size + content_length` (typically
     `4096 + content_length` bytes).

______________________________________________________________________

## 3. URL Canonicalization Specification

Prior to generating MD5 hashes and resolving cache container paths, every URL
passes through `canonicalize_url()`:

```c
char *canonicalize_url(const char *url);
```

### 3.1 Normalization Algorithm

1. **Fragment Stripping:**

   - Any URI fragment (`#fragment`) is removed. Fragments are client-side
     anchors that are never transmitted over HTTP.

1. **Case Normalization:**

   - The scheme (`http`, `https`) and host components are converted to lowercase
     (`HTTP://EXAMPLE.COM` $\\to$ `http://example.com`).

1. **Default Port Removal:**

   - Standard default scheme ports are stripped (`:80` for `http://`, `:443` for
     `https://`).
   - Non-standard ports (`:8080`, `:8443`, `:34521`) are strictly preserved.

1. **Path Dot-Segment & Redundant Slash Resolution:**

   - Relative path segments (`.` and `..`) are resolved per RFC 3986 Section
     5.2.4.
   - Consecutive adjacent slashes in path segments (`//` $\\to$ `/`) are
     collapsed.
   - **Trailing Slash Preservation:** Trailing slashes are strictly preserved
     (e.g., `/archive/` is never collapsed to `/archive`) because trailing
     slashes distinguish directory listings from regular files.

1. **Percent-Encoding Normalization:**

   - Unreserved characters (`[A-Za-z0-9-_.~]`) are decoded.
   - Reserved percent-encoded bytes are normalized to uppercase hex digits (e.g.
     `%2f` $\\to$ `%2F`).

1. **Implementation Standard:**

   - Implemented using libcurl's URL API (`curl_url()`, `curl_url_set()`,
     `curl_url_get()`): the input is parsed with `CURLU_NON_SUPPORT_SCHEME`, the
     path is re-asserted with `CURLU_PATH_AS_IS`, and the result is retrieved
     with `CURLU_NO_DEFAULT_PORT`.

______________________________________________________________________

## 4. Cache Subsystem Workflows & Data Flow

### 4.1 Cache-First Stat Resolution

When populating an uninitialized directory table (`LinkTable_uninitialised_fill`
in `src/link.c`):

```
LinkTable Entry
      │
      ▼
Check CacheContainer_read_head(link->f_url)
      │
      ├─ Cache HIT (Container valid & fresh)
      │    ├─ link->time = stat.remote_mtime
      │    ├─ link->content_length = stat.content_length
      │    └─ link->type = stat.link_type
      │    (0 Network Requests!)
      │
      └─ Cache MISS / Expired
           └─ Queue for asynchronous network HEAD probe
```

1. If all entries within a directory hit the cache,
   `LinkTable_uninitialised_fill()` completes synchronously in `< 0.1 ms`.
1. Entries that miss or have expired are batched and probed concurrently using
   libcurl multi-interface requests.

### 4.2 HEAD Response Persistence

When an asynchronous network `HEAD` request completes in
`filestat_on_complete()` (`src/transfer.c`):

1. The raw HTTP response headers captured in `TransferStruct` are extracted.
1. `Link_classify_response()` determines the resulting `LinkType` (`LINK_DIR`,
   `LINK_FILE`, or `LINK_INVALID`).
1. If the cache system is initialised (`CACHE_SYSTEM_INIT`) and the file size is
   not bypassed by the `--cache-min-size` / `--cache-max-size` thresholds,
   `CacheContainer_write_head()` writes the container:
   - Sets `flags = CACHE_FLAG_IS_HEAD` (plus `CACHE_FLAG_IS_DIR` when the link
     is classified as a directory).
   - Records `http_resp`, `content_length`, `remote_mtime`, `content_type`, and
     `link_type`.
   - Flushes and atomically renames the container into place.
1. If the effective URL differs from the requested URL (a redirect occurred), a
   Redirect Pointer Container is additionally written via
   `CacheContainer_write_redirect()`.

### 4.3 Container Promotion on File Download

When file reading triggers payload acquisition (`Cache_create()` in
`src/cache.c`):

1. If a container file exists with `CACHE_FLAG_IS_HEAD`:
   - The existing `CacheHeader` is read from disk.
   - Segment parameters are calculated: $$\\text{segbc} = \\left\\lceil
     \\frac{\\text{content_length}}{\\text{blksz}} \\right\\rceil$$
   - The header flags are updated to `CACHE_FLAG_IS_SPARSE`.
   - A zeroed segment bitmap array of size `segbc` is appended.
   - Zero padding is written to align the payload to the page-aligned
     `header_size` (typically `4096`).
   - `ftruncate(fd, header_size + content_length)` allocates the sparse payload
     space on the local filesystem.
1. The remote metadata (`remote_mtime`) and raw HTTP response headers are
   preserved from the HEAD container without re-downloading; `cache_time` is
   updated to the promotion time.

### 4.4 Redirect Pointer Traversal & Loop Guard

When opening or inspecting any cache container (`CacheContainer_read_head`,
`CacheContainer_read`, `Cache_create`):

```
Lookup URL
    │
    ▼
Read CacheHeader
    │
     ├─ flags & CACHE_FLAG_IS_REDIRECT
     │    │
     │    ├─ depth > 5 ──► Return 0 (cache miss; circular redirect detected)
     │    │
     │    └─ depth <= 5 ─► Read target URL from payload
     │                      depth = depth + 1
     │                      Recurse lookup for target URL
     │
     └─ Standard Container ──► Serve metadata / data
```

- Guarantees transparent access: consumers querying an unredirected alias URL
  automatically receive data from the canonical target container.
- Depth limit (up to $5$ pointer hops) prevents hanging or crashing on circular
  redirect topologies; exceeding it is treated as a cache miss, so the data is
  refetched from the network.

______________________________________________________________________

## 5. Administrative Operations & Cache Purging

### 5.1 Host-Specific Cache Clearing (`--cache-clear-host`)

The `--cache-clear-host <URL_OR_HOST>` CLI option enables surgical eviction of a
single server's cached data:

1. **URL Argument:** If the argument contains `://`, `get_server_root()`
   extracts the origin (scheme and host:port). It is escaped via
   `curl_easy_escape()`, targeting:
   ```
   <cache_root>/<escaped_server_root>/
   ```
1. **Bare Host Argument:** If the argument lacks a scheme (e.g. `example.com`),
   both `https%3A%2F%2F<host>` and `http%3A%2F%2F<host>` directories are
   targeted.
1. The matching directory tree is recursively deleted via `nftw()` with
   `FTW_DEPTH | FTW_PHYS`.
1. HTTPDirFS exits with `EXIT_SUCCESS` immediately after deletion.

### 5.2 Global Cache Clearing (`--cache-clear`)

The `--cache-clear` option purges all cached data across all origins:

1. Resolves the cache directory (default or overridden by `--cache-location`).
1. Recursively removes all origin folders and `CACHEDIR.TAG`.
1. Exits immediately with `EXIT_SUCCESS`.

______________________________________________________________________

## 6. Architectural Guarantees & Correctness Properties

1. **Alignment & POSIX Direct I/O Safety:**

   - Payloads start at a 4096-byte aligned offset (`header_size`), matching
     standard OS memory page boundaries and disk block sizes. In practice
     `header_size` is `4096`; it grows to the next page boundary only for
     unusually long URLs or HTTP headers.
   - Enables direct kernel page caching and sparse allocation without
     partial-block misalignment penalties.

1. **Crash-Safe Container Writes:**

   - HEAD and redirect pointer containers are written to a temporary file and
     atomically renamed into place, so readers never observe a partial
     container.
   - Directory listing containers and file payload containers are written in
     place (`O_TRUNC`) and flushed (`fflush`) before use; a container whose
     on-disk size is smaller than `header_size + content_length` is treated as
     corrupt and deleted.

1. **Loop and Recursion Safety:**

   - Redirect pointer traversal strictly enforces a depth ceiling of 5 hops,
     guaranteeing termination.
   - Ancestor links (`..`, `/`, parent directory loops) are filtered during HTML
     link parsing before entering the cache subsystem.

1. **Origin Isolation:**

   - Every origin has an isolated, escaped directory path.
   - Cross-origin credentials or custom headers are strictly prohibited from
     leaking across origin directory boundaries.
