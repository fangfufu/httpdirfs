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
anchor tags.

The system is designed around two core tenets:

1. **Universal Anchor-Text Parsing with Progressive Escalation:** Descriptive
   names in HTML link anchor text (`<a>Title</a>`) are preserved as virtual
   filenames while avoiding collisions and preventing link loss.
1. **Deterministic Directory Classification:** Directory detection follows
   strict rules: trailing slashes denote directories by default, while
   `--html-is-directory` allows dynamic promotion of linked HTML resources into
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
      --html-is-directory is OFF (Default)                          --html-is-directory is ON
                 │                                                             │
                 ├─ Content-Type header ignored                                ├─ Content-Type: text/html?
                 ├─ cl == 0 && --zero-len-is-dir ──► LINK_DIR                  │     ├─ cl > max_html_size ──► LINK_FILE
                 └─ cl >= 0                     ──► LINK_FILE                  │     └─ cl <= max_html_size ─► LINK_DIR (Promoted!)
                                                                               └─ Content-Type non-HTML
                                                                                     ├─ cl == 0 && --zero-len-is-dir ──► LINK_DIR
                                                                                     └─ cl >= 0                     ──► LINK_FILE
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

  - **When `--html-is-directory` is Disabled (Default):**

    - The `Content-Type` header is completely ignored.
    - If `Content-Length == 0` and `--zero-len-is-dir` is enabled, the entry is
      converted to `LINK_DIR`.
    - Otherwise, if `Content-Length >= 0`, the entry is confirmed as
      `LINK_FILE`. Linked HTML pages remain downloadable files.
    - If `Content-Length < 0` (e.g. chunked transfer without known length), the
      entry is marked `LINK_INVALID`.

  - **When `--html-is-directory` is Enabled:**

    - The `Content-Type` header is parsed using `is_html_content_type()`.
    - If the response header matches `text/html` (e.g., `text/html`,
      `text/html; charset=utf-8`):
      - If `Content-Length > CONFIG.max_html_size`, the resource is considered
        too large for a directory listing and falls back to a readable
        `LINK_FILE`.
      - If `Content-Length <= CONFIG.max_html_size` (or chunked `cl == -1`), the
        entry is **promoted to `LINK_DIR`**. Browsing into it triggers recursive
        HTML directory listing on that page.
    - If `Content-Type` is non-HTML (e.g. `application/octet-stream`,
      `image/png`, `application/zip`):
      - If `Content-Length == 0 && CONFIG.zero_len_is_dir`, it transitions to
        `LINK_DIR`.
      - Otherwise, it transitions to `LINK_FILE`.

### Phase 3: Directory Download Guard

When `--html-is-directory` is active and HTTPDirFS downloads the HTML payload of
a promoted directory (`write_download_full_callback`), streaming chunks are
monitored. If the incoming payload exceeds `CONFIG.max_html_size`, the transfer
is immediately aborted with `CURLE_WRITE_ERROR` to protect against unbounded
memory allocation.

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
1. Leading dots (`.`) and spaces are trimmed so that entries do not become
   hidden Unix files.

### Step 2: Zero Path Segment Fallback

If the target URL has no path segments (e.g., root URL `/` or query-only):

1. Test if `clean_anchor` is available in the directory's `LinkHashSet`.
1. If taken, append numeric suffixes (`clean_anchor-1`, `clean_anchor-2`, ...,
   up to `-9999`) until an available slot is found.

### Step 3: Progressive URL Path Escalation

When the target URL contains $N$ path segments (`segments[0]` through
`segments[N-1]`):

The algorithm iterates through escalation depths $i = 1, 2, \\dots, N$:

1. **Extract Suffix Segments:** Collect the last $i$ segments from the URL path
   joined by `-`: $$\\text{path_part} = \\text{segments}[N-i] \\mathbin{\\Vert}
   \\text{"-"} \\mathbin{\\Vert} \\dots \\mathbin{\\Vert}
   \\text{segments}[N-1]$$

1. **Redundancy Omission (Depth $i = 1$):** If $i == 1$ and `clean_anchor`
   matches the final path segment `segments[N-1]` case-insensitively, repeating
   the anchor is redundant (e.g. avoiding `file.iso-file.iso`). In this case,
   the candidate is simply: $$\\text{candidate} = \\text{path_part}$$ Otherwise,
   the candidate combines the anchor and path parts: $$\\text{candidate} =
   \\text{clean_anchor} \\mathbin{\\Vert} \\text{"-"} \\mathbin{\\Vert}
   \\text{path_part}$$

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
- When appending `-<suffix>`, the base candidate length is truncated so that
  $\\text{base_len} + \\text{suffix_len} < \\text{NAME_MAX} + 1$.

______________________________________________________________________

## 4. Concrete Walkthrough Examples

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

## 5. Configuration Flags Reference

- **`--html-is-directory`** (default: disabled) Enables dynamic directory
  promotion. Resources returning `Content-Type: text/html` within
  `--max-html-size` are exposed as virtual directories instead of regular files.
- **`--allow-external-origin`** (default: disabled) Permits traversal of links
  pointing to cross-origin servers. When disabled, all links pointing outside
  the mounted server origin are filtered out.
- **`--max-html-size <size>`** (default: `2M`) Maximum size threshold for HTML
  directory promotion. Supports standard unit suffixes (`K`, `M`, `G`).
- **`--ignore-anchors`** (default: disabled) Skips intra-page HTML fragment
  links starting with `#`.
- **`--zero-len-is-dir`** (default: disabled) Treats any file with
  `Content-Length: 0` as a directory.
