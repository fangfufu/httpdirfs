# Directory Detection and Name Collision Resolution Specification

This document specifies the technical design, algorithms, and configuration
flags governing how HTTPDirFS extracts entry names from HTML pages, resolves
name collisions, detects directories, and dynamically promotes linked HTML
resources.

______________________________________________________________________

## 1. Architectural Overview

HTTPDirFS models remote web server directory listings and HTML index pages as a
virtual local filesystem. Unlike traditional FTP or WebDAV protocols, HTTP does
not provide a native directory enumeration API. Instead, HTTPDirFS parses HTML
documents using the Gumbo HTML5 parser and discovers resources via `<a href>`
anchor tags and media/asset references (`<img src>`, `<video src>`,
`<link href>`, `srcset`, ...) as described in Section 4.

The system is designed around two core tenets:

1. **Universal Anchor-Text Parsing with Progressive Escalation:** Descriptive
   names in HTML link anchor text (`<a>Title</a>`) are preserved as virtual
   filenames while avoiding collisions and preventing link loss.
1. **Deterministic Directory Classification:** Directory detection follows
   strict rules: trailing slashes denote directories by default, while
   `--website-mode` allows dynamic promotion of linked HTML resources into
   navigable subdirectories.

______________________________________________________________________

## 2. Directory Detection Pipeline

Directory identification proceeds in two sequential phases: an initial syntax
classification during HTML parsing, followed by network probe verification
during link initialization.

```
HTML Parsing: <a href="...">
      │
      ▼
URL Resolution: resolve_target_url()
      │
      ├─ Target URL path ends with '/' ────► LINK_UNINITIALISED_DIR
      └─ Target URL path has no trailing '/' ─► LINK_UNINITIALISED_FILE
                                                │
                                                ▼
                                    HTTP Probe (HEAD Request)
                                                │
                 ┌──────────────────────────────┴──────────────────────────────┐
                 │                                                             │
       --website-mode is OFF (Default)                          --website-mode is ON
                  │                                                             │
                  ├─ cl >= 0 (known size) ──────────► LINK_FILE                 ├─ Content-Type: text/html?
                  │    (cl == 0 && --zero-len-is-dir ► LINK_DIR)                │     ├─ cl > max_html_size ──► LINK_FILE
                  │                                                             │     ├─ cl <= max_html_size ─► LINK_DIR (Promoted!)
                   └─ cl < 0 (unknown size):                                     │     └─ cl < 0 (unknown size) ─► LINK_DIR
                         ├─ text/html ───────────► LINK_DIR (may be empty folder)│
                         ├─ no Content-Type ─────► LINK_DIR (may be empty folder)│
                          └─ non-HTML real CT ────► LINK_INVALID (hidden)         └─ Content-Type non-HTML / none:
                                                                                       ├─ cl >= 0 (known size) ──► LINK_FILE
                                                                                       │    (cl == 0 && --zero-len-is-dir ► LINK_DIR)
                                                                                       ├─ cl < 0 + non-HTML real CT ► LINK_INVALID (hidden)
                                                                                       └─ cl < 0 + no CT ──────────► LINK_DIR (may be empty folder)
```

### Phase 1: URL Syntax Parsing

During HTML parsing (`HTML_to_LinkTable`), each `<a href>` tag is resolved
against the directory's base URL using `resolve_target_url()`:

1. **Ancestor Loop Check:** If the target URL points to the current directory or
   any ancestor table in the directory tree (`is_ancestor_head_link()`), it is
   discarded immediately.
1. **Origin Scoping:** If `--allow-external-origin` is disabled (the default),
   any link pointing to a cross-origin server (`is_cross_origin()`) is
   discarded.
1. **Syntax Classification:**
   - The resolved target URL is inspected ignoring query strings (`?`) and
     fragments (`#`).
   - If the URL path ends with a slash (`/`), the link is initially classified
     as `LINK_UNINITIALISED_DIR`.
   - If the URL path does not end with a slash, the link is initially classified
     as `LINK_UNINITIALISED_FILE`.

### Phase 2: HTTP Probe & Response Classification

Before presenting virtual files to FUSE, HTTPDirFS probes all uninitialized
entries concurrently using HTTP `HEAD` requests (`CURLOPT_NOBODY`) via
`Link_classify_response()`:

- **Entries with Initial Type `LINK_UNINITIALISED_DIR`:**

  - If the server responds with `HTTP 200 OK`, the entry transitions directly to
    `LINK_DIR`.
  - The `Content-Type` header is not checked; any URL ending with `/` that
    returns HTTP 200 is confirmed as a directory.

- **Entries with Initial Type `LINK_UNINITIALISED_FILE`:**

  - **When `--website-mode` is Disabled (Default):**

    - If the size is known (`Content-Length >= 0`), the entry is confirmed as
      `LINK_FILE` (or `LINK_DIR` if `Content-Length == 0` and
      `--zero-len-is-dir` is enabled). Linked HTML pages remain downloadable
      files.
    - If the size is unknown (`Content-Length < 0`, e.g. chunked / no
      `Content-Length`), the size cannot be reported without downloading the
      whole body, so the decision falls back to the content type:
      - `text/html` is tentatively a directory (`LINK_DIR`); its real size is
        learned on first browse, and an oversized or failed download degrades it
        to an empty folder.
      - A missing or empty `Content-Type` is likewise a tentative directory
        (`LINK_DIR`): on-the-fly generated directory listings often send neither
        a `Content-Type` nor a `Content-Length`, so hiding them would make such
        directories disappear.
      - Any other known non-HTML content type is hidden (`LINK_INVALID`), since
        a file whose size we cannot report is not exposed.

  - **When `--website-mode` is Enabled:**

    - The `Content-Type` header is parsed using `is_html_content_type()`.
    - **`text/html`** (e.g. `text/html`, `text/html; charset=utf-8`):
      - known size `> CONFIG.max_html_size` → readable `LINK_FILE` (decided at
        probe time; it is never a directory).
      - known size `<= CONFIG.max_html_size`, or unknown size (`cl < 0`) →
        **promoted to `LINK_DIR`**. Browsing into it triggers recursive HTML
        directory listing on that page.
    - **A known non-HTML type** (e.g. `application/octet-stream`, `image/png`,
      `application/zip`):
      - `Content-Length == 0 && CONFIG.zero_len_is_dir` → `LINK_DIR`.
      - `Content-Length >= 0` → `LINK_FILE`.
      - unknown size (`cl < 0`) → hidden (`LINK_INVALID`): a concrete non-HTML
        type is trusted to be a file, not a listing, so it is never parsed, and
        its unknown size means it cannot be presented as a file either.
    - **No `Content-Type` (or an empty one):**
      - `Content-Length >= 0` → `LINK_FILE` (as with non-HTML).
      - unknown size (`cl < 0`) → tentatively `LINK_DIR`; parsed on first
        browse, degrading to an empty folder if oversized, failed, or linkless.

### Phase 2.1: Classification Matrix

The rules above reduce to the following matrix for `LINK_UNINITIALISED_FILE`
entries that receive `HTTP 200 OK`:

| Content-Type    | Content-Length                   | `--website-mode` OFF (default)                  | ON                                              |
| --------------- | -------------------------------- | ----------------------------------------------- | ----------------------------------------------- |
| `text/html`     | known, `0 < cl <= max_html_size` | `LINK_FILE`                                     | `LINK_DIR` (promoted)                           |
| `text/html`     | known, `cl > max_html_size`      | `LINK_FILE`                                     | `LINK_FILE`                                     |
| `text/html`     | `cl = 0`                         | `LINK_FILE` (`LINK_DIR` if `--zero-len-is-dir`) | `LINK_DIR`                                      |
| `text/html`     | unknown (`cl < 0`)               | `LINK_DIR` (tentative)                          | `LINK_DIR` (tentative)                          |
| known non-HTML  | `cl = 0`                         | `LINK_FILE` (`LINK_DIR` if `--zero-len-is-dir`) | `LINK_FILE` (`LINK_DIR` if `--zero-len-is-dir`) |
| known non-HTML  | `cl > 0`                         | `LINK_FILE`                                     | `LINK_FILE`                                     |
| known non-HTML  | unknown (`cl < 0`)               | `LINK_INVALID` (hidden)                         | `LINK_INVALID` (hidden)                         |
| missing / empty | `cl = 0`                         | `LINK_FILE` (`LINK_DIR` if `--zero-len-is-dir`) | `LINK_FILE` (`LINK_DIR` if `--zero-len-is-dir`) |
| missing / empty | `cl > 0`                         | `LINK_FILE`                                     | `LINK_FILE`                                     |
| missing / empty | unknown (`cl < 0`)               | `LINK_DIR` (tentative)                          | `LINK_DIR` (tentative)                          |

Pre-conditions (both modes):

- Any entry receiving a non-200 status becomes `LINK_INVALID` (retried later on
  temporary failures).
- Entries with initial type `LINK_UNINITIALISED_DIR` (URL path ends with `/`)
  become `LINK_DIR` without inspecting `Content-Type`.

Matrix notes:

- The unknown-size rows (`cl < 0`) are identical in both modes and are resolved
  on content type alone **before** the `--website-mode` flag is consulted
  (`Link_classify_response()`, `src/transfer.c`): a concrete non-HTML type is
  hidden, while HTML or a missing/empty `Content-Type` is a tentative directory.
- Only two rows change with the flag: `text/html` with `0 < cl <= max_html_size`
  (`LINK_FILE` → `LINK_DIR`, promoted) and `text/html` with `cl = 0` (`LINK_DIR`
  regardless of `--zero-len-is-dir`, since the HTML check runs first). The
  `cl > max_html_size` row stays `LINK_FILE` in both modes.
- **Tentative `LINK_DIR`** (unknown size): parsed on first browse; degrades to
  an empty folder if the listing fails, or (with `--website-mode` enabled)
  exceeds `max_html_size` (Phase 3) — never demoted to a file.
- **Promoted `LINK_DIR`** (flag enabled, HTML): the download is capped at
  `max_html_size` on first browse, with the same empty-folder degradation if it
  exceeds the cap or fails.
- `max_html_size` is `--max-html-size` (default `2M`); it is consulted only when
  `--website-mode` is enabled.

### Phase 3: Oversized / Failed Directory → Empty Folder

A link classified as a directory is **never** turned into a file: a directory
stays a directory for the lifetime of the mount. When `--website-mode` exposes a
page as a directory (a promoted `text/html` page, or a page with no
`Content-Type` and an unknown size), the listing is fetched in `LinkTable_new()`
via `Link_download_full()`, which caps the body at `CONFIG.max_html_size`. If,
on first browse, the listing could not be fetched (HTTP non-200 / empty body) or
exceeds `max_html_size` (the capped download aborts once the cap is exceeded),
the entry is left as an **empty folder** — a directory with no children — rather
than being parsed as a partial listing or demoted to a file:

- The capped/partial body is not parsed and not written to the container cache,
  so the empty folder is re-fetched on a later refresh (once `--refresh-timeout`
  expires) instead of persisting stale contents.
- The same `max_html_size` gate is re-applied to a cached body, so a listing
  cached under a larger limit is treated as an empty folder after the limit is
  lowered (and the now-inconsistent container is deleted).
- The cap (and the cached-body gate) only applies when `--website-mode` is
  enabled. With the flag disabled, a tentative `LINK_DIR` (unknown size) is
  downloaded in full without a cap and parsed normally on first browse.
- **Exemption from the cap** is decided purely by URL syntax: any URL whose path
  (ignoring query string and fragment) ends with `/` is a real directory and is
  downloaded in full, since its listing is unambiguously a directory listing and
  may legitimately exceed `max_html_size`. There is no special exemption for the
  root table: a root mounted from a no-trailing-slash URL is capped like any
  other promoted page and degrades to an empty folder if its listing exceeds
  `max_html_size` (mount with a trailing slash to avoid this). A real directory
  whose listing fails to download also degrades to an empty folder.

______________________________________________________________________

## 3. Name Collision Resolution & Progressive Escalation

In standard web directory listings (e.g., Apache mod_autoindex), the link text
typically mirrors the URL filename (`<a href="foo.iso">foo.iso</a>`). However,
in many web pages, forums, and archives, link texts are identical (e.g.
"Download", "Readme", "ISO"), or the link URL is an opaque identifier (e.g.
`/file/102`).

HTTPDirFS uses `generate_collision_free_name()` to guarantee that every link has
a clean, human-readable name without collisions or data loss.

### Step 1: Text Extraction & Normalization

1. The HTML DOM node is processed using `extract_anchor_text()`, collecting text
   from the anchor and all child elements.
1. Leading and trailing whitespace is stripped.
1. Internal sequences of consecutive whitespace characters (spaces, tabs,
   newlines) are collapsed into single spaces.
1. Path separator slashes (`/`) are replaced with underscores (`_`) to prevent
   unintended subpath creation.

When a candidate name is constructed (Steps 2-4), the anchor is additionally
stripped of leading dots (`.`) and whitespace (yielding `clean_anchor`) so that
entries do not become hidden Unix files.

### Step 2: Zero Path Segment Fallback

If the target URL has no path segments (e.g., root URL `/` or query-only):

1. Test if `clean_anchor` is available in the directory's `LinkHashSet`.
1. If taken, append numeric suffixes (`clean_anchor-1`, `clean_anchor-2`, ...,
   up to `-9999`) until an available slot is found.

### Step 3: Progressive URL Path Escalation

When the target URL contains $N$ path segments (`segments[0]` through
`segments[N-1]`), the last $i$ of them, in order, are denoted $r_1, r_2, …, r_i$
($r_i$ being the final segment `segments[N-1]`). At each depth $i$, the path
part (those segments joined by `-`) is $P_i$ and the candidate name is $C_i$;
$A$ denotes `clean_anchor`.

The algorithm iterates through escalation depths $i = 1, 2, …, N$:

1. **Extract Suffix Segments:** Collect the last $i$ segments from the URL path
   and join them with `-`:

   $$P_i = r_1 | - | … | - | r_i$$

   (A `|` between two parts means the two parts are concatenated, with `-` as
   the joining string.)

   1. **Redundancy Omission (Depth $i = 1$):** If $i == 1$ and `clean_anchor`
      matches the final path segment `segments[N-1]` case-insensitively
      (whitespace runs are ignored on both sides), repeating the anchor is
      redundant (e.g. avoiding `file.iso-file.iso`). In this case, the candidate
      is simply:

      $$C_i = P_i$$

   1. **Empty Anchor:** If `clean_anchor` is empty, the anchor prefix is omitted
      at every depth and the candidate is simply:

      $$C_i = P_i$$

      Otherwise, the candidate combines the anchor and path parts:

      $$C_i = A | - | P_i$$

1. **Collision Probe:** The candidate is probed in `LinkHashSet`:

   - If `candidate` does not exist in the set, it is added and returned
     immediately.
   - If `candidate` already exists, the algorithm escalates to depth $i + 1$,
     prepending the next URL path segment to provide further context.

### Step 4: Exhaustion Fallback (Numeric Suffixing)

If all $N$ path segments are exhausted and every candidate collided:

1. HTTPDirFS takes the final candidate from depth $N$.
1. It tests numeric suffixes `-1`, `-2`, ..., `-9999`.
1. The first available suffixed name is added to `LinkHashSet` and returned.
1. **Guarantee:** Every valid link encountered in the HTML is added; no link is
   silently dropped due to naming clashes.

### Step 5: Filesystem Length Guard

Linux filesystems enforce a maximum filename length of `NAME_MAX` (255 bytes).
When constructing candidates and appending numeric suffixes:

- Suffix buffers are accounted for upfront (`sizeof(candidate)` limited to
  `NAME_MAX + 1`).
- When appending `-<suffix>`, truncate the base candidate so that the total
  stays below $M + 1$: write the base candidate length as $b$ and the appended
  suffix length as $s$, then require $b + s < M + 1$ ($M$ is `NAME_MAX`).

______________________________________________________________________

## 4. Resource Reference Extraction

Besides `<a href>` anchors, HTML documents reference media and asset resources
through dedicated elements and attributes. When `--website-mode` is enabled,
`LinkTable_parse_html()` materializes such resources (images, videos,
stylesheets, scripts, ...) as links through the same pipeline as anchors (URL
resolution, cross-origin filtering, target deduplication, name generation).
Without `--website-mode`, only `<a href>` hyperlinks are extracted and presented
in the mounted tree.

### 4.1 Supported Elements and Attributes

| Element                             | Attribute       | Naming anchor   |
| ----------------------------------- | --------------- | --------------- |
| `a` / `area`\*                      | `href`          | anchor text     |
| `img`                               | `src`, `srcset` | `alt` (see 4.3) |
| `source`                            | `src`, `srcset` | none            |
| `video`, `audio`                    | `src`           | none            |
| `script`                            | `src`           | none            |
| `link` (stylesheet / icon / ...)    | `href`          | none            |
| `iframe`, `frame`, `embed`, `track` | `src`           | none            |
| `object`                            | `data`          | none            |
| `input` (`type="image"` only)       | `src`           | none            |

All other elements and attributes (e.g. `form action`, `input type="text"`) are
ignored.

\* `a` is extracted in all modes; `area` (image map regions) only under
`--website-mode`, together with the rest of the table.

- **`srcset`:** every comma-separated candidate is expanded into its own link;
  width (`480w`) and scale (`2x`) descriptors are stripped.
- **Duplicated targets:** an anchor and a resource reference to the same URL
  produce a single entry; the first reference in document order wins the name.
- Extraction applies to every parsed HTML body (promoted pages, tentative
  directories, regular listings), exactly like anchor extraction — but only
  while `--website-mode` is enabled.

### 4.2 Scheme Filtering

`resolve_target_url()` accepts only the `http` and `https` schemes
(case-insensitive). References carrying any other URI scheme (`data:`,
`javascript:`, `blob:`, `mailto:`, `tel:`, ...) are rejected and never
materialized. This filtering applies in both normal mode and `--website-mode`
(it guards `<a href>` anchors as well as resource references). A colon before
the first `/` identifies a scheme (RFC 3986); a colon that appears only after
the first `/` (e.g. `sub/file:copy.iso`) is not a scheme and resolves as a
relative path.

### 4.3 `<img>` Naming (alt Text)

An `<img>` element uses its `alt` attribute as the naming anchor, subject to the
same collision rules as anchor text. The alt text is ignored and the name falls
back to the URL filename when:

- it is empty or whitespace-only, or
- the same non-empty alt text is used by two or more images on the same page.
  Detection runs as a pre-pass over the whole page, so *all* images carrying a
  duplicated alt fall back — not only the later ones.

______________________________________________________________________

## 5. Concrete Walkthrough Examples

### Example A: Standard Autoindex Link (Redundancy Omission)

- **HTML:** `<a href="/debian/pool/main/v/vim/vim_9.0.deb">vim_9.0.deb</a>`
- **Target URL:** `https://example.com/debian/pool/main/v/vim/vim_9.0.deb`
- **Segments ($N = 6$):**
  `["debian", "pool", "main", "v", "vim", "vim_9.0.deb"]`
- **Anchor:** `"vim_9.0.deb"`
- **Depth 1 ($i = 1$):**
  - Last segment: `"vim_9.0.deb"`.
  - `strcasecmp("vim_9.0.deb", "vim_9.0.deb") == 0` -> Redundancy detected!
  - Candidate: `"vim_9.0.deb"`.
  - Added to hashset -> **Result:** `vim_9.0.deb`.

### Example B: Shared Anchor Text with Distinct Paths (Escalation)

- **Link 1 HTML:** `<a href="/archive/2021/report.pdf">Annual Report</a>`
  - Segments: `["archive", "2021", "report.pdf"]`
  - Depth 1 candidate: `"Annual Report-report.pdf"`.
  - Slot free -> **Result 1:** `Annual Report-report.pdf`.
- **Link 2 HTML:** `<a href="/archive/2022/report.pdf">Annual Report</a>`
  - Segments: `["archive", "2022", "report.pdf"]`
  - Depth 1 candidate: `"Annual Report-report.pdf"` -> **COLLISION!**
  - Escalate to Depth 2 ($i = 2$):
    - Path part: `"2022-report.pdf"`.
    - Candidate: `"Annual Report-2022-report.pdf"`.
    - Slot free -> **Result 2:** `Annual Report-2022-report.pdf`.

### Example C: Identical Anchor and URL Path (Numeric Fallback)

- **Link 1 HTML:** `<a href="/docs/faq">FAQ</a>`
  - Result: `FAQ-faq`.
- **Link 2 HTML:** `<a href="/docs/faq">FAQ</a>` (e.g. repeated in page footer)
  - Dropped by early target URL deduplication (`target_url_set`).
- **Link 3 HTML:** `<a href="/mirror/docs/faq">FAQ</a>`
  - Depth 1 (`FAQ-faq`) -> Collides with Link 1.
  - Depth 2 (`FAQ-docs-faq`) -> Assumed occupied by another entry.
  - Depth 3 (`FAQ-mirror-docs-faq`) -> Assumed occupied.
  - All 3 segments exhausted -> Numeric fallback activates:
  - Appends `-1` -> **Result:** `FAQ-mirror-docs-faq-1`.

______________________________________________________________________

## 6. Configuration Flags Reference

- **`--website-mode`** (default: disabled) Enables dynamic directory promotion.
  Resources returning `Content-Type: text/html` within `--max-html-size` are
  exposed as virtual directories instead of regular files.
- **`--allow-external-origin`** (default: disabled) Permits traversal of links
  pointing to cross-origin servers. When disabled, all links pointing outside
  the mounted server origin are filtered out.
- **`--max-html-size <size>`** (default: `2M`) Maximum size threshold for HTML
  directory promotion. Supports standard unit suffixes (`K`, `M`, `G`). A
  known-size HTML page larger than this is exposed as a regular file. A
  directory whose listing is larger than this (unknown-size pages promoted at
  probe time) is downloaded with the body capped at this limit and, on first
  browse, degrades to an empty folder — a directory is never turned into a file.
- **`--ignore-anchors`** (default: disabled) Skips intra-page HTML fragment
  links starting with `#`.
- **`--zero-len-is-dir`** (default: disabled) Treats any file with
  `Content-Length: 0` as a directory.
