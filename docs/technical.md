## HTTPDirFS Technical Notes

For the normal HTTP directories, this program downloads the HTML web pages/files
using [libcurl](https://curl.haxx.se/libcurl/), then parses the listing pages
using [Gumbo](https://github.com/google/gumbo-parser), and presents them using
[libfuse](https://github.com/libfuse/libfuse).

For \*sonic servers parses \*sonic servers' XML responses using
[expat](https://github.com/libexpat/libexpat).

The filesystem read requests are received by libfuse, are then translated to
HTTP requests by libcurl.

The OS reads files in blocks, therefore the corresponding HTTP download requests
are ranged requests in chunks. Because of these reasons, HTTPDirFS by default
expects the server to support HTTP Range Request. The server can indicate this
feature by presenting `"Accept-Ranges: bytes` in the header responses. You can
disable this check by using the `--no-range-check` flag, however HTTPDirFS will
have to download the entire file before serving you.

The cache system uses a unified single-file container architecture. Each URL
maps to exactly one container file storing its binary header, canonical URL, raw
HTTP response headers, segment download bitmap, and payload data. Each HTTP
server origin gets its own cache directory beneath the cache location root,
named using the escaped server root URL. Within each origin, files are sharded
into 256 subdirectories using the first two hex characters of their canonical
URL's MD5 hash. For the specification of the cache system, please refer to
[specs/cache.md](specs/cache.md).

### Universal Link Parsing and Directory Detection

HTTPDirFS parses HTML listing documents using the Gumbo HTML5 parser. It
extracts descriptive filenames from anchor text, resolves name collisions, and
identifies directories. For complete technical specifications, please refer to
[specs/directory_detection_and_naming.md](specs/directory_detection_and_naming.md).

#### Allowed characters in filenames

The intended allowed character in filenames is the following:

Filenames must:

- Consists of printable characters
- Must not contain '/' in the middle of the filename.
- Must not end with '/'.

Without the `--html-is-directory` flag, directories must:

- Consists of printable characters
- Must not contain '/' in the middle of the directory name.
- Must end with '/'.

#### Universal Parsing Mechanics

- **Anchor Text Filename Extraction:** The text inside `<a>...</a>` tags is
  extracted, sanitized, and used as the virtual file or directory name.
  Whitespace is normalized, slashes (`/`) are converted to underscores (`_`),
  and leading dots and spaces are stripped to avoid hidden Unix files.
- **Progressive Collision Resolution:** If multiple links share identical anchor
  text, HTTPDirFS disambiguates names by combining anchor text with URL path
  segments (e.g., `Readme-readme.txt`, `Readme-38601-readme.txt`). If the anchor
  text already matches the URL segment case-insensitively, redundant prefixing
  is omitted. If all segments are exhausted and collisions persist, numeric
  suffixes (`-1`, `-2`, ...) are appended.
- **Early Duplicate Removal:** If the exact same target URL appears multiple
  times on a page, only the first encountered link and anchor text are kept.
- **Ancestor Loop Prevention:** Links pointing back to the current directory or
  any of its parent directories are discarded to prevent infinite recursive
  loops.
