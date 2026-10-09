# Progressive Directory Preload Specification

This document specifies the technical design of the
`--progressive-directory-preload` option: a background preloading mechanism that
hides directory entries from directory listings until the file count of their
contents is known, and bounds the induced network traffic to the browsed
hierarchy plus one level ahead.

The interaction with entry classification (`LINK_DIR` detection, HTML promotion,
probing) is specified in `docs/specs/directory_detection_and_naming.md`; this
document assumes classification is already settled for every link it touches.

______________________________________________________________________

## 1. Problem and Motivation

A directory's file count is not known until its remote listing is parsed,
because HTTP has no directory enumeration API: the count is the number of links
in the HTML document fetched for the directory.

GUI file managers (e.g., Dolphin/KDE) display per-directory item counts in icon
view. To compute the count they `stat`/list every visible entry. Each visible
subdirectory therefore triggers a synchronous network round-trip in the main
FUSE thread, serially, before the view is drawn. On a mount with many
subdirectories the first browse of any folder is dominated by this fan-out.

The flag makes the cost asynchronous:

- Directory entries in a freshly loaded listing are **hidden from `readdir`**
  until their own listing has been loaded.
- The listings are loaded **in the background** by a dedicated worker thread
  that drives the fetches concurrently on the shared libcurl multi handle.
- An entry reappears in the listing the moment its file count is known — which
  is exactly what the GUI was about to fetch itself, now already done.

______________________________________________________________________

## 2. Behavioral Summary

With `--progressive-directory-preload` enabled (NORMAL mode):

1. When a directory's `LinkTable` is **downloaded and attached for the first
   time by client-initiated code** (mount, root refresh, or `opendir`), every
   `LINK_DIR` child link in that table is marked hidden and enqueued for
   background preloading.
1. The worker loads the listing of each queued directory — from the on-disk
   container cache if it is still fresh, otherwise by fetching (all fetches in a
   batch in flight at once on the shared curl multi handle) — attaches the child
   `LinkTable`, and clears the hidden flag on success.
1. `readdir` reports only links for which `type != LINK_INVALID` **and**
   `hidden == 0`.
1. **One-level-ahead bound:** a `LinkTable` loaded *by the worker* is terminal —
   its directory children are neither hidden nor enqueued. Background preloading
   is only ever scheduled by client code (mount / root refresh / `opendir`),
   never by the worker. Consequently total background traffic equals *browsed
   directories + one level ahead*; a deep tree is never crawled from the root.
1. **Failure policy:** if a preload fetch fails (network error, HTTP error,
   oversized listing, ...), the directory is unhidden anyway, with a warning
   logged. The mount degrades to the current (non-flag) behavior for that entry;
   nothing is ever lost.
1. **Mode scope:** NORMAL mode only. Sonic (`--sonic`) and Single-file
   (`--single-file`) mounts are unaffected; the option is ignored there.

Direct access is unaffected: a hidden directory remains resolvable by full path,
and `opendir` on it loads its listing synchronously in the FUSE thread, exactly
as without the flag.

______________________________________________________________________

## 3. Design Choice: Load-Time Triggering

Scheduling could in principle be hooked in two places:

- **Classification-time:** each directory, the moment its probe/classification
  settles to `LINK_DIR`, would enqueue itself. Correctly cutting the
  one-level-ahead cascade then requires knowing *which code path* created the
  table, which forces a per-table flag (e.g. `loaded_by_preload`), a changed
  `LinkTable_new()` signature, new hooks inside `transfer.c` completion paths,
  and per-table refcount bookkeeping.
- **Load-time (chosen):** a standalone function
  `Link_preload_directories(LinkTable *tbl)` is invoked only from the small
  number of *client-side* table-load completion points. Worker-loaded tables are
  terminal by construction, because worker code simply never calls the function.

The load-time design was selected because it achieves the same cascade cut with
no per-table state and no `LinkTable_new()` signature change. The only per-link
state introduced is a single `int hidden` field.

______________________________________________________________________

## 4. Components

### 4.1 `Link.hidden`

`struct Link` gains `int hidden;`, zero-initialized in `Link_new()`.

- `hidden == 1` means: the link is a directory whose file count is not yet
  known; it must be omitted from `readdir` output.
- Only `LINK_DIR` links are ever set hidden. Files, virtual links (`.httpdirfs`
  diagnostics), and `LINK_INVALID` links are never affected.
- The field is written without `link_lock`, matching the existing lock-free
  update pattern of `Link.type` in the transfer layer. (See Section 8 for why
  the resulting race is benign.)

### 4.2 `Link_should_list(const Link *link)`

Single predicate used by `fs_readdir` (src/fuse_local.c):

```
link->type != LINK_INVALID && !link->hidden
```

Centralizing the filter here keeps `fs_readdir` unchanged apart from the call
and makes the listing rule unit-testable.

### 4.3 The table loaders

`LinkTable_load_and_attach(Link *link, int *created_new)` is the on-demand
(client-side) loader used by `path_to_LinkTable()`. Given a link, it returns a
`LinkTable` for the directory the link names, downloading one if none is already
attached:

1. Under `link_lock`: check `link->next_table`; if present, return it (with the
   existing expiry/retirement handling), `*created_new = 0`.
1. Otherwise, outside the lock: dispatch to the mode-appropriate loader (NORMAL:
   `LinkTable_new(link->f_url, link->parent_table)`); on failure or
   `index_time <= 0`, log a warning and return `NULL`.
1. Under `link_lock`: if `link->next_table` is still unset, attach
   (`next_table`, `parent_tbl`, `parent_link`, refcounts, `orphaned = 0`); if
   another thread won the race, free the fresh table and return the existing
   one.
1. Returns the table carrying **exactly one reference for the caller**;
   `*created_new` is `1` iff a fresh table was attached by this call.

`path_to_LinkTable()` is refactored to use this helper (it keeps its
`--invalid-refresh` handling and its parent-table unref).

The load-then-attach sequence that both loaders perform is split into shared
pieces, so the synchronous NORMAL loader and the asynchronous preload path
(Section 4.5) reuse exactly the same code:

- `LinkTable_try_load_cached(url, parent_tbl)` — build a table from a *fresh*
  cached container: read the raw response (headers + HTML payload) from the
  on-disk cache, re-apply the current origin policy and the `max_html_size` gate
  to the cached body (invalidating the entry when they no longer hold), and
  parse it. Returns NULL when the cache system is off or the listing is not
  cached / expired, in which case the caller downloads.
- `LinkTable_begin_listing(url, parent_tbl)` — allocate the table (fresh
  `index_time`, head link) and link it to its parent. The listing download runs
  under the table's head link.
- `LinkTable_finish_listing(tbl, url, ts, header)` — apply a downloaded body to
  a begun table: a failed fetch marks the table failed (`index_time == 0`), an
  empty or capped body keeps it an empty folder, otherwise parse, fill, and save
  the raw response to the container cache. Consumes the transfer buffers.
- `LinkTable_attach_loaded(link, tbl)` — the attach-under-`link_lock` tail,
  shared with `LinkTable_load_and_attach()` (both run it through the small
  internal helper `attach_new_table()`); frees a failed table and leaves the
  entry without one so the next access retries.

`LinkTable_new()` (the synchronous NORMAL loader) is `try_load_cached`, then
`begin` + `Link_download_full()` + `finish` on a cache miss; the preload worker
(Section 4.5) is `try_load_cached`, then `begin`, `Link_setup_full_download`,
`finish`, and `attach_loaded` on a cache miss, driving the download
asynchronously instead of calling `Link_download_full()`.

### 4.4 `Link_preload_directories(LinkTable *tbl)`

The scheduling hook. Guarded no-op unless **all** of the following hold:

- `CONFIG.progressive_dir_preload` is set,
- `CONFIG.mode` is NORMAL,
- `tbl` is non-NULL and `tbl->index_time > 0` (the listing actually loaded).

Otherwise, for every link `i > 0` in `tbl` with
`type == LINK_DIR && !is_virtual`:

```
link->hidden = 1;
LinkTable_ref(tbl);          /* queue-lifetime reference (Section 7) */
Preload_enqueue(link);
```

### 4.5 Preload subsystem (`src/preload.c`, new)

- Unbounded FIFO queue of `{ Link *link }` items, guarded by a mutex and a
  condvar; `Preload_queue_depth()` exposes the depth for diagnostics and tests.
- A **single worker thread**, spawned by `Preload_start()` from the FUSE `init`
  callback (`fs_init`). That callback runs in the process that serves the mount
  — in daemon (background) mode the daemon child, i.e. after the daemonizing
  fork — which is why the worker must not be spawned in `main()`. Items enqueued
  before the worker starts are drained once it does. If the worker cannot be
  spawned, `Preload_start()` unhides every queued link so the entries degrade to
  on-demand loading.
- **Cache check.** Before starting a fetch for a queued link, the worker calls
  `LinkTable_try_load_cached()`. A fresh cached listing is built into a table
  from the container and finalized (attach, unhide, release) in place with no
  network round-trip, exactly as the synchronous loader would serve it; only a
  cache miss falls through to the download below.
- **Asynchronous fetches.** The worker pops the whole queue as one batch and
  starts every fetch in it *concurrently* on the shared curl multi handle (no
  thread per fetch; I/O concurrency is libcurl's). For each queued link that
  missed the cache it begins a listing table (`LinkTable_begin_listing`),
  configures the head link's download (`Link_setup_full_download`), and adds the
  easy handle with `transfer_nonblocking()`. A per-fetch `PreloadFetch` carries
  the state machine: `TransferStruct` body + header, the in-flight `CURL *`, a
  `retry_at` deadline, and a `finished` flag. The worker then pumps
  `curl_multi_perform_once()` (a ≤100 ms blocking poll, not a busy loop) until
  every fetch in the batch is finished, requeuing handles whose
  temporary-failure deadline has come due.
- **Completion callback.** Each `TransferStruct` carries
  `on_complete = preload_on_complete` and `user_data` pointing at its
  `PreloadFetch`. The callback runs under `transfer_lock`, on *whichever* thread
  pumped the multi handle (worker or FUSE), and mirrors the synchronous
  `Link_download_full()` state machine:
  - transport error (result ≠ 0, no cap) → mark the table failed;
  - 3xx → follow one redirect hop via `Transfer_follow_redirect()` (cross-origin
    rejected, `MAX_REDIRECTS` enforced), resetting the buffers and requeueing
    the handle with `transfer_requeue_locked()`;
  - temporary HTTP failure (429 / Cloudflare) → record a `retry_at` deadline
    (`CONFIG.http_wait_sec`); the worker requeues when it comes due;
  - other non-200 → mark the table failed;
  - 200 → extract `CURLINFO_FILETIME`/`CURLINFO_EFFECTIVE_URL`, write the
    redirect container, clean up the handle, mark finished. Because
    `on_complete` only does bookkeeping and lock-free requeueing, it is safe to
    run on any thread.
- After the batch is finished, the worker finalizes each fetch:
  `LinkTable_finish_listing()` parses the body (or marks the table failed/empty)
  and `LinkTable_attach_loaded()` attaches it under `link_lock`, racing
  idempotently with concurrent on-demand loads. The link is then unhidden and
  its queue-lifetime reference released.
- At process exit an `atexit` handler sets a stopping flag, broadcasts, and
  joins the worker; in-flight fetches and pending retry deadlines are aborted
  (not waited out) so the process can exit, and the worker finalizes its
  in-flight batch before returning.

______________________________________________________________________

## 5. Scheduling Points (Call Sites)

`Link_preload_directories()` is called from exactly three client-side completion
points, all outside `link_lock`:

| #   | Location                                  | Fires when                                                                                                                                                                                |
| --- | ----------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | `LinkSystem_init()`                       | The root table finished loading at mount. Runs in `main()` pre-FUSE; items are queued then, and the worker (started from the FUSE `init` callback) drains them before any client request. |
| 2   | `check_and_refresh_root_table()`          | A background root refresh produced and attached a *new* root table.                                                                                                                       |
| 3   | New-table branch of `path_to_LinkTable()` | An `opendir`/path resolution downloaded and attached a fresh table, **and** this call won the attach race (`created_new == 1`).                                                           |

The winner-only condition at site 3 is mandatory, not cosmetic: in the loser
branch the fresh table is freed immediately via `LinkTable_free()`
(refcount-ignoring), so enqueuing links of a losing table would hand worker
threads pointers into freed memory.

**Deliberately omitted site:** the new-table branch of
`path_to_Link_recursive()` also creates intermediate `LinkTable`s — but only
while resolving a *deep direct path* (e.g. typing `/a/b/c/file.iso` in the
address bar), with no prior `opendir` of the intermediate directories. Its
win/lose attach decision is made under `link_lock`, whereas
`Link_preload_directories()` must run unlocked; supporting it would require an
accumulator threaded through the recursion to collect the winning tables and
invoke the hook once, after the lock is released. Omitting it means those
intermediate tables are never swept: their `LINK_DIR` children are neither
hidden nor enqueued, so no one-level-ahead preload is scheduled for them. The
target path still resolves and reads normally (direct access is unaffected by
the flag); the only effect is that those intermediate directories' listings are
fetched on demand (synchronously, as without the flag) instead of being
preloaded. GUI browsing — the common case — always goes through `opendir`, so it
is fully covered by site 3. See Section 13.

Tables that are merely **reused** (a `next_table` already existed, or an
`--invalid-refresh` refill of an existing table) are never re-swept: nothing
resets `LINK_INVALID` links in place, so a refill cannot produce new `LINK_DIR`
links, and a reused table's children are already in whatever final state that
table reached.

______________________________________________________________________

## 6. Worker Pipeline

```
Client FUSE thread (mount / root refresh / opendir)
  │
  │  new LinkTable T downloaded & attached
  ▼
Link_preload_directories(T)              guards: flag, NORMAL, T valid
  │  per LINK_DIR non-virtual link L in T:
  │     L.hidden = 1
  │     LinkTable_ref(T)                 queue-lifetime ref
  │     Preload_enqueue(L)
  ▼
FIFO queue (mutex + condvar)
   │
   ▼
Worker (single thread; fetches run concurrently on the shared curl multi)
    │  pop the whole queue as one batch
    │  per L in the batch:                no locks held
    │     tbl = LinkTable_try_load_cached(L)
    │     fresh hit: finalize L (attach/unhide/unref), no fetch
    │     miss:      tbl = LinkTable_begin_listing(L)
    │                start the head-link download, transfer_nonblocking(curl)
    │  pump curl_multi_perform_once()     callbacks run under transfer_lock:
   │     until every fetch is finished        redirects / retries / cap / 200
   │     (requeue due retry deadlines)
   │  per finished fetch:
   │     LinkTable_finish_listing(tbl, ts, header)   parse / mark failed
   │     t = LinkTable_attach_loaded(L, tbl)         attach under link_lock
   │     L.hidden = 0
   │     LinkTable_unref(t)                 attach's caller ref
   │     LinkTable_unref(L->parent_table)   queue-lifetime ref
   ▼
L appears in next readdir of T
```

The worker never calls `Link_preload_directories()`. A table it loads (and
attaches to `L->next_table`) therefore has its children fully visible the moment
it exists — the one-level-ahead invariant (Section 2.4).

______________________________________________________________________

## 7. Lifetime and Reference Counting

Each queued item carries one extra reference to the parent table
(`L->parent_table`), taken at enqueue time and released by the worker after
processing. This reference is load-bearing:

- Without it, a directory that the GUI opens and immediately closes
  (`releasedir` → `orphaned = 1` + unref) could drop its table to
  `refcount == 0` and be freed while its children's preloads are still queued —
  a use-after-free in the worker. With the per-item reference, the parent table
  (and thus every link inside it) is guaranteed alive for the entire queue →
  process window.
- No orphan-flag restoration is needed: when the final queue reference is
  dropped and `orphaned` is set, the normal `LinkTable_unref()` free path runs,
  including its parent-chain unref. A table replaced by a refresh is thus freed
  only after its queued preloads drain (bounded delay).
- The worker's own unref of the freshly attached table (`t`) only removes the
  helper's caller reference; the attach reference that keeps an attached table
  rooted in its parent remains until the parent's lifecycle ends, matching
  existing table semantics.

______________________________________________________________________

## 8. Concurrency and Locking

Three locks are involved, none nested across types:

- `link_lock` (existing): table tree and `next_table`/refcount/orphaned state.
  Held briefly inside `LinkTable_load_and_attach()` and inside
  `LinkTable_ref()`; never held while performing network I/O.
- `q_lock` + condvars (new): preload queue only. Held only for enqueue/dequeue
  bookkeeping, never across the load.
- `transfer_lock` (existing): around `curl_multi_perform_once()`; taken by the
  worker exactly like by FUSE threads — preloads and on-demand loads share the
  same connection pool and the same serialization point. The preload completion
  callback (`preload_on_complete`) runs *inside* this lock, on whichever thread
  pumped, and is limited to bookkeeping plus the lock-free
  `transfer_requeue_locked()`. The worker's read of a fetch's `finished` flag is
  ordered after the lock release of the thread that set it (every
  `curl_multi_perform_once()` re-takes `transfer_lock`), so no extra
  synchronization is needed.

Lock ordering is therefore trivially acyclic (`q_lock` independent; `link_lock`
independent; `transfer_lock` independent).

**Benign races:**

- `readdir` reads `hidden` and `type` lock-free. If it observes `hidden == 0`
  before the `next_table` attach becomes visible to its thread, the directory is
  listed one `readdir` earlier than necessary; any subsequent `opendir` takes
  `link_lock` (acquire) and always sees the attach. No torn state is observable:
  worst case is a slightly early or slightly late appearance, never a wrong
  count.
- `opendir` racing a worker on the same directory: both call
  `LinkTable_load_and_attach()`; the loser frees its duplicate table and uses
  the winner's. `hidden = 0` is idempotent. The directory may simply have been
  listed while still hidden — the next `readdir` shows it.
- A parent table refreshed/retired while its preloads are queued: the new table
  is swept again by site 2/3 (children hidden and re-queued); the old table is
  freed only after its queue references drain (Section 7).

______________________________________________________________________

## 9. Failure Modes and Edge Cases

| Case                                                           | Behavior                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Preload fetch fails (network / HTTP error / oversized listing) | Warning logged; `hidden` cleared anyway; entry visible with on-demand loading (current behavior).                                                                                                                                                                                                                                                                                                                                                                            |
| Root table fails to load at mount (`index_time <= 0`)          | Hook guards on `index_time > 0`; nothing hidden, nothing queued; mount behaves as today.                                                                                                                                                                                                                                                                                                                                                                                     |
| Empty remote directory                                         | Loads fine, count 0, unhidden immediately.                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| Virtual links (`.httpdirfs/*`)                                 | Never hidden, never queued (`is_virtual` check).                                                                                                                                                                                                                                                                                                                                                                                                                             |
| Non-NORMAL mode (Sonic / Single-file)                          | Option ignored entirely.                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| Directory opened directly while hidden                         | `opendir` loads synchronously (blocking, as today); worker's later `hidden = 0` is a no-op.                                                                                                                                                                                                                                                                                                                                                                                  |
| GUI closes a directory while its preloads are queued           | Queue references keep the table alive until the queue drains (Section 7).                                                                                                                                                                                                                                                                                                                                                                                                    |
| Table with many subdirectories                                 | All are enqueued as one batch and all fetches run in flight at once, capped only by the shared multi handle's connection pool (`CURLMOPT_MAX_TOTAL_CONNECTIONS` = `--max-conns`). No depth cap exists (see Section 13).                                                                                                                                                                                                                                                      |
| FUSE daemonization (no `-f`)                                   | Supported. The worker is spawned from the FUSE `init` callback, which runs in the daemon child after the daemonizing fork, so it is never orphaned. Items the parent enqueued before the fork are inherited in the child's memory and drained by the child's worker. libcurl state at fork time is quiescent and single-threaded (only the synchronous root-table load ran in the parent), which is what makes reusing the inherited `CURLM`/`CURL_SHARE` in the child safe. |

______________________________________________________________________

## 10. Filesystem Notification Behavior

New entries become visible on the **next `readdir`** of the containing
directory. HTTPDirFS uses the FUSE high-level API and does not emit
`IN_CREATE`/`FUSE_NOTIFY_INVAL_INODE`-style notifications, so a GUI view already
drawn will show new entries only upon its next refresh (F5, refocus, or any
listing-triggering event). This is a presentation delay, not a correctness
issue: path-based access to a not-yet-shown entry works at all times.

______________________________________________________________________

## 11. Worked Example

```
Mount /srv (flag on). One worker thread; up to 6 concurrent connections.

t0  Client: root table loaded (site 1)
    /                    shown: file.iso only
    ├─ a/                hidden, queued
    ├─ b/                hidden, queued
    └─ file.iso          shown

t1  Worker: a/ listing loaded & attached
    a/ is unhidden; a's children are terminal (all shown):
    /a/
    ├─ a1/               shown immediately (never hidden)
    └─ a2/               shown immediately (never hidden)

t2  Worker: b/ listing loaded & attached; b/ shown.
    Queue empty. No deeper fetch has occurred.

t3  User opens /a in the GUI:
    contents already on disk (t1) → no synchronous fan-out.

t4  User opens /a/a1:
    Client: a1's table downloaded (site 3)
    /a/a1/
    ├─ a1x/              hidden, queued   (one level ahead of /a/a1)
    └─ a1y/              hidden, queued

t5  Worker loads a1x/, a1y/ (both in flight at once); they appear.
    Their children are shown immediately. Total background fetches so far
    = exactly {a, b, a1x, a1y} = browsed directories + one level ahead.
```

______________________________________________________________________

## 12. Testing

**Unit tests (`tests/test_link.c`):**

- `Link_should_list()`: visible for `LINK_FILE` and unhidden `LINK_DIR`; hidden
  for `hidden == 1` `LINK_DIR`; never visible for `LINK_INVALID`.
- `Link_preload_directories()` no-op conditions: flag off; mode != NORMAL;
  `tbl->index_time <= 0`.
- Sweep semantics: on a fresh table with files, directories, a virtual directory
  and an invalid link, only the plain `LINK_DIR` links are hidden and enqueued;
  `Preload_queue_depth()` equals the number of plain directories.
- Worker end-to-end (with the network sub-system initialised): a queued
  directory whose fetch fails fast (connection refused on port 1) is unhidden,
  attaches no table, and releases its queue-lifetime reference — exercising the
  real asynchronous completion path.
- `LinkTable_load_and_attach()`: an already-attached `next_table` is returned
  with `*created_new == 0` and no re-download.
- `LinkTable_try_load_cached()`: NULL with the cache system off or when no
  container exists; on a fresh container a table is built with the parsed links
  and `index_time` set to the container's `cache_time`; an expired container
  returns NULL (the caller downloads).
- Worker cached path: a queued directory whose listing is already cached fresh
  is unhidden with its cached listing attached and no network fetch issued.

**Integration tests (`tests/integration`):**

- Local server with a root containing N subdirectories, each with a file. Mount
  with the flag in foreground mode (`-f`): poll until all N subdirectories
  appear; verify the content of one.
- The same mount without `-f` (FUSE daemon / background mode): the daemonized
  child spawns the worker from the FUSE `init` callback, and all N
  subdirectories still appear — regression guard against the daemonizing fork
  orphaning the worker.

______________________________________________________________________

## 13. Known Gaps and Limitations

1. **No appearance notification:** entries surface on the next `readdir`
   (Section 10).
1. **Deep direct-path access** (Section 5, omitted site): resolving a full path
   directly (e.g. typing `/a/b/c/file.iso` in the address bar) without first
   `opendir`-ing the intermediate directories loads those tables via
   `path_to_Link_recursive()`, which never calls `Link_preload_directories()`.
   Their `LINK_DIR` children are therefore not hidden or enqueued, and no
   one-level-ahead preload is scheduled for them. The path still resolves and
   reads normally; the intermediate directories' listings are simply fetched on
   demand (synchronously, as without the flag) rather than preloaded. GUI
   browsing is unaffected (always via `opendir` → site 3).
1. **Unbounded queue depth:** a listing page with thousands of subdirectories
   enqueues all of them in one batch. Connection concurrency is capped by the
   shared multi handle (`--max-conns`), and total work equals what the GUI would
   eventually stat anyway, but a per-table batch cap (e.g.
   `--preload-max-batch`) is a possible future refinement for hostile/very large
   listings.
1. **Root refresh sweep** happens at refresh time (site 2); if a refresh reuses
   the existing root (no expiry), no sweep occurs — consistent with the "fresh
   table only" rule.

______________________________________________________________________

## 14. Configuration Flags Reference

- **`--progressive-directory-preload`** (default: disabled, no argument) Hides
  directory entries from listings until their file count is known, and loads
  their listings in the background on a single worker thread (fetches run
  concurrently on the shared curl multi handle, connection-capped by
  `--max-conns`). NORMAL mode only. Background work is bounded to browsed
  directories plus one level ahead. Failed preloads degrade to the default
  behavior. New entries appear on the directory's next re-read. Works in both
  foreground (`-f`) and daemon (background) mode: the worker is started from the
  FUSE `init` callback, after any daemonizing fork (see Section 9).
