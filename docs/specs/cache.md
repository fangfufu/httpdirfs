# Unified Single-File Cache Architecture Specification

This document specifies the technical architecture of HTTPDirFS's unified
single-file cache subsystem: the on-disk container format, cache key derivation,
freshness and invalidation model, in-memory data structures and concurrency
model, and the end-to-end workflows that connect the FUSE layer, the transfer
layer, and the on-disk containers.

The cache is **opt-in** (`--cache`). When enabled, every cached HTTP resource —
directory listing, file payload, `HEAD` stat metadata, or redirect pointer — is
stored in a single container file per URL.

______________________________________________________________________

## 1. Architectural Principles

The cache engine is designed around eight core architectural tenets:

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

   - HTTP `HEAD` response metadata (status code, `Content-Length`, remote
     `Last-Modified` timestamp, `Content-Type`) is persisted in a lightweight
     container before any file payload is downloaded.
   - When directories are re-opened or links are shared across multiple pages,
     file stats resolve from local disk without issuing network round trips.
   - HEAD freshness is tracked **independently** of payload freshness via the
     `head_cache_time` header field, so a re-validated `HEAD` refreshes stat
     freshness without touching the payload's `cache_time`.
   - A payload container is **never downgraded** to HEAD-only metadata: once
     segments have been downloaded, a later `HEAD` probe preserves the data
     container and (if the remote is confirmed unchanged) refreshes only
     `head_cache_time` in place.
   - Progressive promotion: when payload bytes are subsequently requested, the
     existing container is promoted in place to a sparse file payload container
     without altering its path or hash identity.

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

   - Each container records the local capture timestamp (`cache_time`), the HEAD
     refresh timestamp (`head_cache_time`), and the remote server's
     `Last-Modified` timestamp (`remote_mtime`) in its binary header.
   - Freshness is deterministically evaluated against `CONFIG.refresh_timeout`
     in three distinct domains (Section 4):
     - `HEAD` stat metadata: age of `head_cache_time` (falling back to
       `cache_time`).
     - Directory listing payloads: age of `cache_time`.
     - File payload containers at open time: remote-unchanged short-circuit
       (both `remote_mtime` and `content_length` match the live link ⇒ valid
       regardless of age), otherwise age of `cache_time`.
   - Invalidation is immune to filesystem timestamp quirks (progressive writes
     updating `st_mtime`, `cp` resetting timestamps, ...).

1. **Deterministic URL Canonicalization:**

   - Every URL is normalized via `canonicalize_url()` before hashing and cache
     path resolution.
   - Guarantees that semantically equivalent URLs resolve to the exact same
     cache container.

1. **Transparent HTTP Redirect Aliasing (Pointer Containers):**

   - When a requested URL redirects (HTTP 301, 302, 307, 308) or the effective
     URL diverges from the request URL, HTTPDirFS persists:
     1. The canonical container under `canonicalize_url(target_url)`.
     1. A lightweight Redirect Pointer Container (`CACHE_FLAG_IS_REDIRECT`)
        under `canonicalize_url(source_url)`.
   - Pointer containers are followed transparently with an iteration depth guard
     to prevent infinite redirect loops.

______________________________________________________________________

## 2. On-Disk Layout & Binary Specification

### 2.1 Cache Root and Directory Sharding

Cache root resolution (`CacheSystem_get_cache_root()` in `src/cache.c`):

1. If `--cache-location <dir>` is given, `<dir>` **is** the cache root.
1. Otherwise the XDG cache home is used: `$XDG_CACHE_HOME` when set to an
   **absolute** path (an empty or relative value is treated as unset), falling
   back to `$HOME/.cache`, then to `./.cache`; the root is
   `<cache home>/httpdirfs/`.

Under the cache root:

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

- **Origin Directory**: Extracted via `get_server_root(url)` (scheme, host,
  non-default port) and escaped using `curl_easy_escape()`.
- **Shard Directory (`ab/`)**: Characters `0..1` of the 32-character hexadecimal
  MD5 hash (`generate_md5sum(key)`).
- **Container Filename (`<hash>`)**: The complete 32-character MD5 hash string.
- `CacheSystem_init()` creates the cache root, writes `CACHEDIR.TAG`, and
  creates the origin directory for the mounted URL; `CACHE_DIR` is then set to
  that per-origin directory, and all container paths in this document are
  relative to it.

______________________________________________________________________

### 2.2 Unified Container File Format

Every container file begins with a fixed 64-byte packed binary `CacheHeader`,
followed by variable-length metadata sections and page-aligned payload data:

```
+-------------------------------------------------------------------------+
| CacheHeader (Fixed size: 64 bytes, packed)                              |
|   - magic (uint32_t): 0x53464448 ("HDFS" in little-endian)              |
|   - version (uint16_t): 1                                               |
|   - flags (uint16_t): Bitmask (see Section 2.3)                         |
|   - url_len (uint32_t): Byte length of the stored source URL string     |
|   - header_size (uint32_t): Byte offset where payload data begins       |
|   - http_header_len (uint32_t): Byte length of raw HTTP response header |
|   - cache_time (int64_t): Local time(NULL) when the payload was         |
|                            downloaded / container created               |
|   - remote_mtime (int64_t): Remote Last-Modified timestamp (0 if none)  |
|   - content_length (int64_t): Total length of payload in bytes          |
|   - blksz (int32_t): Segment block size in bytes (0 for HEAD-only and   |
|                                             repurposed for redirect)    |
|   - segbc (int32_t): Total segment count (0 for HEAD-only / redirect)   |
|   - reserved (uint8_t[4]): Reserved for future extensions (all zero)    |
|   - head_cache_time (int64_t): Local time when HEAD metadata was        |
|                            refreshed (0 on pre-existing containers;     |
|                            readers fall back to cache_time)             |
+-------------------------------------------------------------------------+
| Source URL String (url_len bytes + null terminator)                     |
+-------------------------------------------------------------------------+
| Raw HTTP Response Headers (http_header_len bytes: status line + headers)|
+-------------------------------------------------------------------------+
| Segment Bitmap (segbc bytes: uint8_t seg[] array, omitted if segbc==0)  |
+-------------------------------------------------------------------------+
| 4 KiB Page Alignment Padding (zero bytes up to the next page boundary)  |
+-------------------------------------------------------------------------+ <-- header_size
| Payload Data (HTML text OR file binary, content_length bytes)           |
| (Omitted on disk for HEAD-only containers)                              |
+-------------------------------------------------------------------------+
```

Section offsets are computed by `container_compute_layout()`:

$$\\text{http_header_offset} = 64 + \\text{url_len} + 1$$

$$\\text{bitmap_offset} = \\text{http_header_offset} + \\text{http_header_len}$$

$$\\text{header_size} = \\lceil (64 + \\text{url_len} + 1 +
\\text{http_header_len} + \\text{segbc}) / 4096 \\rceil \\times 4096$$

#### Field Specifications

| Field             | Type       | Size (Bytes) | Description                                                                                                                                             |
| ----------------- | ---------- | ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `magic`           | `uint32_t` | 4            | Magic signature: `0x53464448` (ASCII `"HDFS"`, LE)                                                                                                      |
| `version`         | `uint16_t` | 2            | Container format version (currently `1`)                                                                                                                |
| `flags`           | `uint16_t` | 2            | Bitmask defining container state and type                                                                                                               |
| `url_len`         | `uint32_t` | 4            | Length of stored source URL (null terminator is written but not counted)                                                                                |
| `header_size`     | `uint32_t` | 4            | Byte offset where payload data begins (4096-byte aligned)                                                                                               |
| `http_header_len` | `uint32_t` | 4            | Byte length of raw HTTP response headers                                                                                                                |
| `cache_time`      | `int64_t`  | 8            | Local POSIX timestamp when the payload was downloaded                                                                                                   |
| `remote_mtime`    | `int64_t`  | 8            | Upstream `Last-Modified` timestamp (POSIX seconds, or `0`)                                                                                              |
| `content_length`  | `int64_t`  | 8            | Total payload size in bytes (from `Content-Length`; target URL length for redirect pointers)                                                            |
| `blksz`           | `int32_t`  | 4            | Download segment block size (default 8 MiB; `0` for HEAD-only; redirect status code for redirect pointers)                                              |
| `segbc`           | `int32_t`  | 4            | Segment count in bitmap: $\\lceil \\text{content_length} / \\text{blksz} \\rceil$ (`0` for HEAD-only / redirect)                                        |
| `reserved`        | `uint8_t`  | 4            | Reserved for future use (must be set to zero)                                                                                                           |
| `head_cache_time` | `int64_t`  | 8            | Local POSIX timestamp of the last `HEAD` metadata refresh; `0` for containers written before this field existed, in which case readers use `cache_time` |

The struct is declared `__attribute__((packed))` and its 64-byte size is
enforced by a `_Static_assert`.

______________________________________________________________________

### 2.3 Bitmask Flags

```c
#define CACHE_FLAG_IS_COMPLETE 0x1  /* Payload is fully downloaded */
#define CACHE_FLAG_IS_SPARSE   0x2  /* Payload region was sparse-allocated */
#define CACHE_FLAG_IS_HEAD     0x4  /* Container contains cached HEAD / file stat */
#define CACHE_FLAG_IS_DIR      0x8  /* Resource represents an HTML directory listing */
#define CACHE_FLAG_IS_REDIRECT 0x10 /* Container is an HTTP redirect pointer */
```

`CACHE_FLAG_IS_DIR` is a type annotation combined with the state flags; the
state flags are `IS_COMPLETE`, `IS_SPARSE`, `IS_HEAD`, and `IS_REDIRECT`
(mutually exclusive).

______________________________________________________________________

### 2.4 Container Archetypes

Depending on its flags, a container file exists in one of four distinct states:

```
                       ┌────────────────────────┐
                       │    HTTP HEAD Probe     │
                       └───────────┬────────────┘
                                   │
                ┌──────────────────┴───────────────────────┐
                │ link classified LINK_DIR                 │ link classified LINK_FILE
                ▼                                          ▼
   ┌──────────────────────────────┐        ┌──────────────────────────────┐
   │   HEAD-Only Container        │        │   HEAD-Only Container        │
   │  (IS_HEAD | IS_DIR)          │        │   (IS_HEAD)                  │
   └──────────────┬───────────────┘        └──────────────┬───────────────┘
                  │ listing downloaded                    │ first open (payload
                  │                                       │ request)
                  ▼                                       ▼
   ┌──────────────────────────────┐        ┌──────────────────────────────┐
   │ Directory Listing Container  │        │  Sparse File Payload         │
   │ (IS_SPARSE|IS_COMPLETE|      │        │  (IS_SPARSE)                 │
   │  IS_DIR)                     │        │  bitmap tracks segments;     │
   │                              │        │  complete when all bits set  │
   └──────────────────────────────┘        └──────────────────────────────┘
```

1. **HEAD-Only Container (`CACHE_FLAG_IS_HEAD`, optionally
   `| CACHE_FLAG_IS_DIR`):**

   - Created upon completing an HTTP `HEAD` probe (when the link is not
     bypassed, see Section 6.10).
   - Stores `content_length`, `remote_mtime`, and the raw HTTP response headers
     (including `Content-Type`). When no raw headers were captured but a
     `Content-Type` is known, a minimal synthetic header block
     (`HTTP/1.1 <code> OK\r\nContent-Type: <ct>\r\n\r\n`) is stored instead.
   - `segbc = 0`, `blksz = 0`: no bitmap, no payload. `header_size` is the
     4096-aligned size of the metadata sections (typically `4096`); the file
     itself is **not** padded to `header_size`.
   - Written atomically to a temporary file (`<container>.tmp.<pid>`), then
     renamed into place.
   - **On-disk size:** `64 + url_len + 1 + http_header_len` (typically 300–600
     bytes; no payload bytes allocated).
   - An existing payload container (sparse or complete) is **never replaced** by
     a HEAD-only write; see Section 4.4.

1. **Directory Listing Container
   (`CACHE_FLAG_IS_SPARSE | CACHE_FLAG_IS_COMPLETE | CACHE_FLAG_IS_DIR`):**

   - Created upon downloading the HTML payload of a directory listing, keyed by
     the **effective** URL the listing was served from.
   - Stores raw HTTP headers and the full HTML text.
   - `segbc = ⌈payload_len / blksz⌉` (i.e. `1` for any payload smaller than the
     segment block size, which covers all listings under the default
     `--max-html-size`). Every bitmap segment is marked present.
   - `remote_mtime = 0`; listing freshness is purely `cache_time`-based.
   - Payload begins at offset `header_size` (4096-aligned, typically `4096`).
   - Written in place (`O_CREAT | O_TRUNC`), flushed before close; the file is
     unlinked if any write fails.

1. **Sparse File Payload Container (`CACHE_FLAG_IS_SPARSE`):**

   - Created when a cached file is opened (promotion of a HEAD-only container or
     a fresh creation), see Section 6.4.
   - The file is extended to `header_size + content_length` using `ftruncate()`
     so the payload region is sparse-allocated.
   - The segment bitmap array (`uint8_t seg[segbc]`, stored at `bitmap_offset`)
     records which blocks are present on disk:
     - `seg[i] == 0`: Block not yet downloaded (reads would return zero bytes).
     - `seg[i] == 1`: Block downloaded and written.
   - The on-disk header **keeps the `IS_SPARSE` flag even after the last segment
     is downloaded**; there is no flag transition. Completeness is derived by
     inspecting the bitmap: a file container is complete iff every bit is set.
     Readers that require a complete payload (e.g. cached listing loads)
     therefore scan the bitmap rather than trusting `IS_COMPLETE`.
   - The bitmap is re-persisted after every segment write and on close, so a
     restarted or crashed process never re-downloaded already-present segments.

1. **Redirect Pointer Container (`CACHE_FLAG_IS_REDIRECT`):**

   - Created when the effective URL differs from the requested URL (HTTP 301,
     302, 307, or 308), both for `HEAD` probes and full downloads.
   - `url_len`: Byte length of the canonical **source** URL.
   - `content_length`: Byte length of the canonical **target** URL string (no
     null terminator).
   - `blksz`: Repurposed to store the HTTP redirect status code (e.g. `301`).
   - Payload section: Contains the canonical target URL, exactly
     `content_length` bytes, starting at offset `header_size`.
   - No-ops when source and target are identical after canonicalization.
   - Written atomically to a temporary file, then renamed into place.
   - **On-disk size:** `header_size + content_length` (typically
     `4096 + content_length` bytes).

______________________________________________________________________

## 3. Cache Key Derivation & URL Canonicalization

### 3.1 Cache Key Source

The string that is hashed into the container path is chosen per link by
`cache_key_source()`:

- **SONIC mode:** the stable track id (`link->sonic.id`). Stream URLs in Sonic
  mode embed per-session authentication tokens, so they are not stable across
  sessions; the track id is the durable identity of the resource.
- **All other modes (NORMAL, SINGLE):** the link's URL (`link->f_url`).

### 3.2 URL Canonicalization

Prior to generating MD5 hashes and resolving cache container paths, every URL
passes through `canonicalize_url()` (`src/url.c`):

```c
char *canonicalize_url(const char *url);
```

Normalization algorithm:

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

### 3.3 Hashing and Sharding

`string_to_cache_path(source)` computes `generate_md5sum(source)` (lowercase
32-character hex, OpenSSL MD5) and returns the shard-relative path
`<hash[0..1]>/<hash>`. `url_to_cache_path(url)` is the convenience wrapper that
canonicalizes first.

### 3.4 Redirect Resolution at Key Computation

When a cache entry is **opened, created, or deleted for a link**
(`cache_key_for_link()`), the derived path is passed through
`resolve_redirect_fn()`, which follows redirect pointer containers on disk
(reading each pointer's target URL and re-deriving the path) up to **5 hops**.
If the pointer chain exceeds the depth limit or is broken, the original path is
used and a fresh fetch will repair the chain. This guarantees that a redirected
URL and its canonical target always resolve to the **same** payload container,
so segment downloads and bitmap state are shared.

Payload *reads* (listings, HEAD) follow pointer chains inside their read
functions (Sections 4 and 6) with the same depth guard.

______________________________________________________________________

## 4. Freshness & Invalidation Model

All freshness checks are deterministic functions of the container header and
`CONFIG.refresh_timeout` (default 3600 s). There are three freshness domains:

### 4.1 HEAD / Stat Metadata

`CacheContainer_read_head(url, &stat)`:

1. Resolves the container path (canonical key, no pointer pre-resolution;
   redirect pointers are followed during the read, depth ≤ 5).
1. Fresh iff $\\text{time(NULL)} - \\text{head_stamp} \\le
   \\text{refresh_timeout}$, where $\\text{head_stamp} =
   \\text{head_cache_time}$ when non-zero, else `cache_time`.
1. On a hit, returns `content_length`, `remote_mtime`, `link_type` (`IS_DIR` ⇒
   `LINK_DIR`, else `LINK_FILE`), `http_resp = 200`, and the `Content-Type`
   parsed from the stored raw headers.
1. Accepts containers flagged `IS_HEAD`, `IS_COMPLETE`, or `IS_SPARSE` — i.e. a
   payload container doubles as a stat source.
1. A container that fails magic/version validation is treated as corrupt and
   unlinked.

### 4.2 Directory Listing Payloads

`CacheContainer_read(_with_time)(url, ...)`:

1. Follows redirect pointer chains (depth ≤ 5) to the payload container.
1. Rejects `IS_HEAD` containers (no payload) and returns "not cached".
1. Validates geometry: `content_length > 0`, `header_size` a multiple of 4096,
   `url_len ≤ PATH_MAX`, `segbc > 0`, and on-disk size
   `≥ header_size + content_length`.
1. **Completeness:** the payload must be complete — `IS_COMPLETE` set, or (for
   containers keeping `IS_SPARSE`) every bit of the on-disk bitmap set. A
   partial file payload is never served as a listing (undownloaded segments read
   back as zero bytes).
1. **Key match:** the stored source URL is canonicalized and must equal the
   requested canonical key (a non-canonical stored `f_url` does not invalidate a
   valid container).
1. **Freshness:** $\\text{time(NULL)} - \\text{cache_time} \\le
   \\text{refresh_timeout}$.
1. On success, returns the payload, the raw HTTP headers, the container
   `cache_time` (used as the loaded table's `index_time`), and the resolved
   source URL (the base for relative link resolution).
1. Any corruption detected on read unlinks the container.

### 4.3 File Payload Containers at Open Time

Two validation stages guard `Cache_open()`:

**`Cache_exist(fn)`** (cheap pre-check):

- File must exist and be at least 64 bytes; a header-less stub is deleted.
- Magic and version must validate.
- Flags must include `IS_SPARSE` or `IS_COMPLETE`.
- `IS_SPARSE` containers are accepted **regardless of age** — their
  already-downloaded segments are re-validated against the live link in the next
  stage.
- Non-sparse (complete) containers must satisfy $\\text{age} \\le
  \\text{refresh_timeout}$.

**`Container_read(cf)`** (full validation):

1. Magic / version.
1. `content_length > 0`, `blksz > 0`, `segbc > 0` (zero-byte files are not
   cached).
1. `header_size ≥ 4096` and a multiple of 4096.
1. Stored source URL matches the live link's cache key source exactly.
1. **Remote-unchanged short-circuit:** if `remote_mtime` and the live link's
   `time` are both known and equal, **and** the stored `content_length` equals
   the live `content_length`, the remote object has not changed and the
   container is valid **regardless of age** — downloaded segments are never
   re-downloaded.
1. Otherwise, age-based invalidation: $\\text{time(NULL)} - \\text{cache_time} >
   \\text{refresh_timeout}$ ⇒ stale.
1. If both `remote_mtime` values are known and differ ⇒ stale.
1. If the stored `content_length` differs from the live value ⇒ stale.
1. `segbc` must equal $⌈\\text{content_length}/\\text{blksz}⌉$ (computed from
   the header, not the link).
1. Loads the segment bitmap into memory.

A stale or corrupt container is deleted and a fresh one created (Section 6.4).

### 4.4 In-Place HEAD Refresh

When a `HEAD` probe completes for a URL that already has a **payload** container
(sparse or complete), `CacheContainer_write_head()` does not replace it:

- If `remote_mtime` and `content_length` both confirm the remote object is
  unchanged, only `head_cache_time` is refreshed — an 8-byte `pwrite()` at
  offset `CACHE_HEADER_SIZE − 8` — and the container is left intact. This keeps
  stat resolution cache-hot without resetting the payload's `cache_time`.
- If the remote object changed, the container is preserved anyway; stale payload
  data is rejected at the next open by the Section 4.3 checks.

______________________________________________________________________

## 5. In-Memory Data Structures & Concurrency Model

### 5.1 The `Cache` Structure

Each open file payload has one in-memory `Cache` instance (`src/cache.h`),
shared by all FUSE file handles on the same link:

| Field                                                | Type      | Role                                                              |
| ---------------------------------------------------- | --------- | ----------------------------------------------------------------- |
| `cache_opened`                                       | `int`     | Open reference count (FUSE `open`/`release` pairs)                |
| `fp`                                                 | `FILE *`  | Handle to the single container file                               |
| `path`                                               | `char *`  | Shard-relative container path (`ab/<hash>`)                       |
| `link`                                               | `Link *`  | Back-pointer to the owning link (circular ref, `link->cache_ptr`) |
| `blksz`, `segbc`, `seg`                              | —         | Segment geometry and in-memory bitmap                             |
| `header_size`, `bitmap_offset`, `http_header_offset` | —         | On-disk section offsets                                           |
| `seek_lock`                                          | mutex     | Serializes all `fseeko`/`fread`/`fwrite` payload I/O              |
| `w_lock`                                             | mutex     | Guards the bitmap, segment checks, and writes                     |
| `dl_lock`                                            | mutex     | Guards `active_dls`, `waiters`, shutdown state                    |
| `active_dls`                                         | list head | ActiveDownload trackers for in-flight segments                    |
| `waiters`                                            | `int`     | Count of threads blocked/joining downloads                        |
| `shutdown_cond`, `shutting_down`                     | —         | Drain synchronization at close/free                               |
| `bgt_sem`                                            | semaphore | Background download worker slots (`num_bg_workers`)               |
| `num_bg_workers`                                     | `int`     | `max(1, CONFIG.max_conns / 2)`                                    |

### 5.2 Reference Counting and Lifecycle

- `Cache_open(path)` resolves the link, then under the **global** `cf_lock`
  returns the existing `link->cache_ptr` (incrementing `cache_opened`) or
  creates a new instance and links it circularly (`cf->link` /
  `link->cache_ptr`).
- `Cache_close(cf)` decrements `cache_opened` under `cf_lock`; while the count
  is non-zero nothing else happens. At zero it:
  1. Drains all background workers (`SEM_WAIT` on `bgt_sem` `num_bg_workers`
     times), so no `Cache_bgdl` thread outlives the instance.
  1. Persists the final bitmap under `w_lock`.
  1. Closes the container file, clears `link->cache_ptr`, and frees the instance
     (waiting on `shutdown_cond` until `waiters` reaches zero).
- The circular link/cache reference is what lets a FUSE `fh` carry only the
  `Cache *` pointer; the owning `LinkTable` reference is released on the last
  close.

### 5.3 Locking Model

| Lock        | Scope     | Protects                                                                        |
| ----------- | --------- | ------------------------------------------------------------------------------- |
| `cf_lock`   | global    | Open/close of `Cache` instances, `link->cache_ptr`                              |
| `seek_lock` | per-Cache | All seek+I/O on `cf->fp` (one stream, many threads)                             |
| `w_lock`    | per-Cache | Bitmap (`seg[]`), segment existence checks, payload writes                      |
| `dl_lock`   | per-Cache | `active_dls` list, `waiters`, `shutting_down`, per-download condition variables |

**Lock ordering: always `w_lock` before `dl_lock`.** Locks are dropped while
launching threads, touching `bgt_sem`, or performing network downloads, so the
read-segment path uses double-checked locking to re-verify state after each drop
(Section 6.5).

### 5.4 ActiveDownload Trackers

`ActiveDownload` nodes (one per in-flight segment, keyed by segment offset) are
the deduplication and synchronization primitive for concurrent reads:

- Added under `dl_lock` when a segment download starts (sync or background).
- The transferring `TransferStruct` is registered on the node (`ad->ts`);
  waiters can **early-return** by copying bytes directly from the in-progress
  transfer buffer (`ts->data`) once enough of the segment has arrived, without
  waiting for the whole download.
- Reference counted: one list reference, one reference held by the transfer
  (`ts->ad_ptr`), one per waiting thread. The node is freed only at zero;
  `ActiveDownload_remove()` broadcasts `ad->cond` when unlinking.
- Waiters block on `ad->cond` under `dl_lock`; after the broadcast they re-check
  the bitmap and fall back to a synchronous download if the segment is still
  missing (e.g. the downloader failed).

### 5.5 Background Download Workers

- Each `Cache` instance allows `num_bg_workers = max(1, max_conns / 2)`
  concurrent background downloads, throttled by `bgt_sem` (initialized to
  `num_bg_workers`).
- `SEM_TRYWAIT` on `bgt_sem` is the only admission test: on failure the caller
  does a synchronous download instead.
- Background threads are **detached** (`Cache_bgdl`); each posts `bgt_sem` on
  exit (success or failure). `Cache_close` and instance teardown rely on the
  semaphore to know when all workers have finished.

______________________________________________________________________

## 6. Subsystem Workflows & Data Flow

### 6.1 Initialization

1. `--cache` sets `CONFIG.cache_enabled`.
1. During mount, `LinkSystem_init(url)` calls `CacheSystem_init(url)` when
   enabled: initializes the global `cf_lock`, computes and creates the cache
   root (`CACHEDIR.TAG`) and the escaped-origin directory for the mounted URL,
   sets `CACHE_DIR`, and sets `CACHE_SYSTEM_INIT = 1`.
1. On process exit, `CacheSystem_cleanup()` frees `CACHE_DIR` and destroys the
   global lock.

All cache write/read entry points no-op (or return "miss") when
`CACHE_SYSTEM_INIT` is 0.

### 6.2 Cache-First Stat Resolution

When populating an uninitialized directory table
(`LinkTable_uninitialised_fill()` in `src/link.c`):

```
LinkTable Entry (LINK_UNINITIALISED_*)
       │
       ▼
Check CacheContainer_read_head(link->f_url)
       │
       ├─ HIT (container valid & fresh)
       │    ├─ link->time = stat.remote_mtime
       │    ├─ link->content_length = stat.content_length
       │    └─ link->type = stat.link_type
       │    (0 Network Requests)
       │
       └─ MISS / Expired
            └─ Queued for asynchronous network HEAD probe
```

1. If all entries within a directory hit the cache,
   `LinkTable_uninitialised_fill()` completes without any network I/O.
1. Entries that miss or have expired are batched and probed concurrently using
   the libcurl multi interface.

### 6.3 HEAD Response Persistence

When a network `HEAD` request completes in `filestat_on_complete()`
(`src/transfer.c`):

1. Redirects are followed one hop at a time (cross-origin targets rejected per
   the origin policy); the effective URL of the final hop is captured.
1. `Link_set_file_stat()` resolves the link's type, size, and mtime.
1. If `CACHE_SYSTEM_INIT` is set and the link is not bypassed (Section 6.10),
   `CacheContainer_write_head()` persists the container **under the effective
   URL**:
   - `flags = IS_HEAD` (plus `IS_DIR` for directories).
   - Records raw headers (or a synthetic `Content-Type` header),
     `content_length`, `remote_mtime`, and classifies `link_type`.
   - Preserves any existing payload container, refreshing only `head_cache_time`
     in place when the remote is confirmed unchanged (Section 4.4).
   - Otherwise written to a temporary file and renamed atomically.
1. If the effective URL differs from the requested URL, a Redirect Pointer
   Container is additionally written via `CacheContainer_write_redirect()`.

### 6.4 File Open and Container Promotion

`Cache_open(path)` (invoked by FUSE `fs_open`):

1. Resolves the link and its container path (with redirect resolution, Section
   3.4).
1. Up to two attempts:
   - **Attempt 1:** if `Cache_exist()` reports a usable container, open it;
     `Container_open()` + `Container_read()` validate header, key, freshness,
     geometry, and load the bitmap (Section 4.3). The on-disk size must cover
     `header_size + content_length`.
   - **Attempt 2 (on any failure):** delete the container and create a fresh
     sparse one via `Cache_create()`.
1. The resulting instance is attached to the link (circular reference) and
   returned as the FUSE file handle.

**Promotion details** (`Container_create()`):

1. If a HEAD-only container exists for the key, its raw HTTP headers are read
   back and carried over into the new container (no re-download of metadata).
1. `segbc = ⌈content_length / blksz⌉`; a zeroed bitmap is allocated.
1. Header, URL, preserved HTTP headers, zeroed bitmap, and zero padding up to
   the 4096-aligned `header_size` are written in place (`O_TRUNC`).
1. `ftruncate(header_size + content_length)` sparse-allocates the payload
   region.
1. `cache_time` records the promotion time; `head_cache_time` is initialized to
   the same value.

### 6.5 File Read Path

FUSE `fs_read` → `Cache_read(cf, buf, len, offset)`:

1. The request is clamped to `content_length` and split into chunks aligned to
   the segment grid (`blksz`); each chunk is served by `Cache_read_segment()`.

`Cache_read_segment()` state machine:

```
retry:
  w_lock:
    segment present in bitmap? ──yes──► Data_read() ──────────────────► bgdl
        │no
    dl_lock:
      in-flight download for this offset? ──yes──► wait on ad->cond
            │                                      (early-return copy from
            │                                       ts->data when enough
            │                                       bytes have arrived)
            │no (back off)
    bgt_sem free slot? ──yes──► add ActiveDownload (double-checked),
        │                        launch detached Cache_bgdl thread,
        │                        goto retry
        │no
  sync_dl:
    dl_lock: re-verify not tracked (double-checked); add ActiveDownload
    unlock; Link_download() one HTTP Range request for the whole segment
    w_lock:
      verify full/last-segment size; Data_write(); Seg_set(1);
      Container_write_bitmap()  (persist)
    dl_lock: remove ActiveDownload, broadcast
    copy requested bytes from the download buffer
bgdl:
  next segment missing? ──yes, bgt_sem free──► launch background prefetch
```

Key properties:

- **Deduplication:** concurrent readers of the same segment never download it
  twice; joiners wait (or early-return from the transfer buffer).
- **Partial-write safety:** a segment is marked present only after the full
  block (or the final partial block) has been written, and the bitmap is flushed
  to disk immediately, so a crash never leaves a half-segment marked complete.
- **Prefetch:** after serving a chunk, if the *next* segment is missing and a
  background slot is free, a detached worker starts downloading it.
- **Backpressure:** when all background slots are busy, the requesting thread
  downloads synchronously, bounding total in-flight downloads to
  `num_bg_workers + concurrent synchronous readers`.

`Cache_close()` drains background workers and persists the final bitmap (Section
5.2), so a re-open later in the same or a future process resumes exactly where
the bitmap left off.

### 6.6 Directory Listing Cache Read/Write

**Write** — `LinkTable_finish_listing()` (`src/link.c`), after a successful full
download: the raw HTTP response (headers + HTML body) is stored via
`CacheContainer_write(parse_url, ...)` keyed by the **effective** URL (the base
for relative links). Failed or capped listings (empty body, `max_html_size`
exceeded) are not cached.

**Read** — `LinkTable_new()` → `LinkTable_try_load_cached()`:

1. `CacheContainer_read_with_time(url, ...)` loads a fresh, complete listing
   (Section 4.2), following any redirect pointer chain.
1. Policy re-application on the cached body:
   - **Cross-origin:** if the resolved listing URL is cross-origin relative to
     the request and `--allow-external-origin` is now disabled, both the alias
     and target containers are deleted and the listing is refetched.
   - **Size gate:** a cached listing for a promoted (non-trailing-slash)
     directory that now exceeds `--max-html-size` is treated as an empty folder
     and both containers are deleted.
1. On a hit, the `LinkTable` is regenerated in memory (`LinkTable_parse_html()`
   against the resolved effective URL) and the table's `index_time` is the
   container's `cache_time`.
1. On a miss, the listing is fetched normally and cached (step "Write" above).

### 6.7 Redirect Pointer Traversal & Loop Guard

All container reads that can encounter a pointer container
(`CacheContainer_read_*`, `CacheContainer_read_head_*`, and
`resolve_redirect_fn` at key computation) follow the chain with a strict depth
ceiling of **5 hops**:

```
Lookup URL
    │
    ▼
Read CacheHeader
    │
    ├─ flags & CACHE_FLAG_IS_REDIRECT
    │    │
    │    ├─ depth > 5 ──► Treat as cache miss (circular/broken chain)
    │    │
    │    └─ depth ≤ 5 ─► Read target URL from payload
    │                     depth++, recurse lookup for target
    │
    └─ Standard Container ──► Serve metadata / data
```

Exceeding the depth limit is treated as a cache miss, so the data is refetched
from the network and the chain repaired by a fresh write.

### 6.8 Preload Worker Integration

With `--progressive-directory-preload` (NORMAL mode), the background preload
worker (`src/preload.c`) is a consumer of the same container cache:

- Before fetching a queued directory, the worker calls
  `LinkTable_try_load_cached()`; a fresh cached listing is materialized directly
  with **no network round-trip** (spec:
  `docs/specs/progressive_directory_preload.md`).
- When a preload fetch succeeds via a redirect, the worker writes the
  corresponding Redirect Pointer Container, keeping the cache consistent with
  the synchronous path.

### 6.9 FUSE Integration

| FUSE op   | Cache interaction                                                                                          |
| --------- | ---------------------------------------------------------------------------------------------------------- |
| `open`    | `Cache_open(path)` → `Cache *` stored in `fi->fh`; bypass cases below                                      |
| `read`    | `Cache_read((Cache *)fi->fh, buf, size, offset)`; bypass → `path_download()` (direct ranged GET, no cache) |
| `release` | `Cache_close((Cache *)fi->fh)`                                                                             |

When the cache is disabled entirely (`!CACHE_SYSTEM_INIT`), `read` always uses
`path_download()` and `open` stores no handle.

### 6.10 Bypass Conditions

A resource is served without touching the container cache when:

1. The cache is not enabled (`--cache` absent).
1. The link is **virtual** (e.g. Sonic index/navigation entries) — FUSE open
   returns the `BYPASS_FH` sentinel.
1. The file is **empty** (`content_length == 0`); zero-byte files are
   unsupported by the container format and open with a null handle.
1. **Size thresholds** (`--cache-min-size` / `--cache-max-size`, both default
   disabled at −1): a file outside the inclusive size range bypasses both HEAD
   persistence (Section 6.3) and open-time caching (Section 6.9).
1. The link is not classified `LINK_FILE` or `LINK_DIR` (invalid links are never
   HEAD-cached).

______________________________________________________________________

## 7. Configuration & CLI Options

| Option                            | Config field              | Default  | Effect                                                                        |
| --------------------------------- | ------------------------- | -------- | ----------------------------------------------------------------------------- |
| `--cache`                         | `cache_enabled`           | off      | Enables the entire cache subsystem                                            |
| `--cache-location <dir>`          | `cache_dir`               | —        | Uses `<dir>` as the cache root (origin subdirs appended)                      |
| `--dl-seg-size <size>`            | `data_blksz`              | `8M`     | Segment/block size; must be ≥ 1 MiB                                           |
| `--refresh-timeout <sec>`         | `refresh_timeout`         | `3600`   | Freshness limit for all three domains (Section 4)                             |
| `--cache-min-size <size>`         | `cache_min_size`          | −1 (off) | Skip caching files smaller than this                                          |
| `--cache-max-size <size>`         | `cache_max_size`          | −1 (off) | Skip caching files larger than this                                           |
| `--max-conns <n>`                 | `max_conns`               | `6`      | Caps background download workers: `max(1, n/2)` per file                      |
| `--cache-clear`                   | `cache_clear`             | —        | Purge the whole cache root, then exit                                         |
| `--cache-clear-host <h>`          | `cache_clear_host`        | —        | Purge one origin's cache, then exit                                           |
| `--progressive-directory-preload` | `progressive_dir_preload` | off      | Background listing preloader; a consumer of the container cache (Section 6.8) |

Size options accept suffixes (e.g. `1M`, `8M`, `1G`). `--cache-clear` and
`--cache-clear-host` are executed after the full argument list is parsed (so
option order and `--config` files cannot change their effect) and are mutually
exclusive.

______________________________________________________________________

## 8. Administrative Operations & Cache Purging

### 8.1 Host-Specific Cache Clearing (`--cache-clear-host`)

Surgical eviction of a single server's cached data:

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
   `FTW_DEPTH | FTW_PHYS | FTW_MOUNT`.
1. HTTPDirFS exits with `EXIT_SUCCESS` immediately after deletion (or
   `EXIT_FAILURE` if no valid argument was given).

### 8.2 Global Cache Clearing (`--cache-clear`)

1. Resolves the cache root (default or overridden by `--cache-location`).
1. Recursively removes all origin folders and `CACHEDIR.TAG` via `nftw()`.
1. Exits immediately with `EXIT_SUCCESS`.

______________________________________________________________________

## 9. Architectural Guarantees & Correctness Properties

1. **Alignment & POSIX Safety:**

   - Payloads start at a 4096-byte aligned offset (`header_size`), matching
     standard OS memory page boundaries and disk block sizes. In practice
     `header_size` is `4096`; it grows to the next page boundary only for
     unusually long URLs or HTTP headers.
   - Enables kernel page caching and sparse allocation without partial-block
     misalignment penalties.

1. **Crash-Safe Container Writes:**

   - HEAD and redirect pointer containers are written to a temporary file and
     atomically renamed into place, so readers never observe a partial
     container.
   - Directory listing and file payload containers are written in place
     (`O_TRUNC`) and flushed; a container whose on-disk size is smaller than
     `header_size + content_length` (or that fails header validation) is treated
     as corrupt and deleted on sight.
   - The segment bitmap is flushed after every segment write and on close, so
     durability of "segment downloaded" survives crashes and restarts.

1. **No Partial Payloads Ever Served:**

   - A segment is marked present only after its full block has been written;
     complete-payload readers (listing loads) verify the entire bitmap before
     serving, so zero-filled holes can never reach the client.

1. **No Duplicate Downloads:**

   - `ActiveDownload` tracking plus double-checked locking guarantees at most
     one in-flight download per segment per file; concurrent readers join the
     existing download.

1. **Bounded Concurrency:**

   - Background prefetch is throttled to `max(1, max_conns / 2)` workers per
     open file; `Cache_close` drains all workers before the instance is freed,
     ruling out use-after-free of the cache structure.

1. **Loop and Recursion Safety:**

   - Redirect pointer traversal strictly enforces a depth ceiling of 5 hops
     (both at key computation and in reads), guaranteeing termination; a broken
     or circular chain degrades to a cache miss and a network refetch.
   - Ancestor links (`..`, `/`, parent directory loops) are filtered during HTML
     link parsing before entering the cache subsystem.

1. **Origin Isolation:**

   - Every origin has an isolated, escaped directory path.
   - Cross-origin credentials or custom headers are strictly prohibited from
     leaking across origin directory boundaries (redirects crossing origins are
     rejected hop-by-hop in the transfer layer).
