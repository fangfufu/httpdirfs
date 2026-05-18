#!/usr/bin/env python3
"""HTTP server with Range request support for HTTPDirFS integration tests.

Python's built-in SimpleHTTPRequestHandler does not support HTTP Range
requests.  HTTPDirFS relies on Range requests to fetch file segments, so we
need a handler that honours the Range header and responds with 206 Partial
Content when appropriate.

Usage:
    python3 range_http_server.py <directory> <port> <port_file>

The server writes its actual listening port to <port_file> once ready, then
serves <directory> until terminated.
"""

import http.server
import os
import signal
import socketserver
import sys
import urllib.parse


class RangeHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP request handler with Range header support."""

    def log_message(self, format, *args):
        """Suppress default request logging."""
        pass

    def send_head(self):
        """Common code for GET and HEAD commands (with Range support).

        Returns a file object, or None if an error response was sent.
        """
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            # Delegate directory listing to the base class
            return super().send_head()

        try:
            f = open(path, "rb")
        except OSError:
            self.send_error(404, "File not found")
            return None

        try:
            fs = os.fstat(f.fileno())
            file_size = fs.st_size

            range_header = self.headers.get("Range")
            if range_header:
                return self._handle_range_request(f, file_size, fs, path)

            # No Range header — send the whole file
            ctype = self.guess_type(path)
            self.send_response(200)
            self.send_header("Content-type", ctype)
            self.send_header("Content-Length", str(file_size))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header(
                "Last-Modified", self.date_time_string(fs.st_mtime)
            )
            self.end_headers()
            return f
        except Exception:
            f.close()
            raise

    def _handle_range_request(self, f, file_size, fs, path):
        """Handle a request containing a Range header."""
        range_header = self.headers.get("Range")
        try:
            # Parse "bytes=START-END"
            unit, ranges_str = range_header.split("=", 1)
            if unit.strip().lower() != "bytes":
                raise ValueError("Unsupported range unit")

            # We only support a single range
            range_spec = ranges_str.split(",")[0].strip()
            start_str, end_str = range_spec.split("-", 1)

            if start_str:
                start = int(start_str)
                end = int(end_str) if end_str else file_size - 1
            elif end_str:
                # Suffix range: last N bytes
                suffix_len = int(end_str)
                start = max(0, file_size - suffix_len)
                end = file_size - 1
            else:
                raise ValueError("Invalid range")

            # Clamp to file size
            end = min(end, file_size - 1)

            if start > end or start >= file_size:
                self.send_error(416, "Requested Range Not Satisfiable")
                self.send_header(
                    "Content-Range", f"bytes */{file_size}"
                )
                self.end_headers()
                f.close()
                return None

            content_length = end - start + 1
            ctype = self.guess_type(path)

            self.send_response(206)
            self.send_header("Content-type", ctype)
            self.send_header("Content-Length", str(content_length))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header(
                "Content-Range",
                f"bytes {start}-{end}/{file_size}",
            )
            self.send_header(
                "Last-Modified", self.date_time_string(fs.st_mtime)
            )
            self.end_headers()

            f.seek(start)
            return _RangeFile(f, content_length)

        except (ValueError, IndexError):
            # Malformed range — fall back to full file
            f.seek(0)
            ctype = self.guess_type(path)
            self.send_response(200)
            self.send_header("Content-type", ctype)
            self.send_header("Content-Length", str(file_size))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header(
                "Last-Modified", self.date_time_string(fs.st_mtime)
            )
            self.end_headers()
            return f

    def do_HEAD(self):
        """Handle HEAD request."""
        f = self.send_head()
        if f:
            f.close()


class _RangeFile:
    """Wraps a file object to limit reads to a byte range."""

    def __init__(self, f, length):
        self._f = f
        self._remaining = length

    def read(self, n=-1):
        if self._remaining <= 0:
            return b""
        if n < 0 or n > self._remaining:
            n = self._remaining
        data = self._f.read(n)
        self._remaining -= len(data)
        return data

    def close(self):
        self._f.close()


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True
    allow_reuse_port = True


def main():
    if len(sys.argv) != 4:
        print(
            f"Usage: {sys.argv[0]} <directory> <port> <port_file>",
            file=sys.stderr,
        )
        sys.exit(1)

    directory = sys.argv[1]
    port = int(sys.argv[2])
    port_file = sys.argv[3]

    os.chdir(directory)

    handler = RangeHTTPRequestHandler

    with ReusableTCPServer(("127.0.0.1", port), handler) as httpd:
        actual_port = httpd.server_address[1]
        with open(port_file, "w") as f:
            f.write(str(actual_port))

        signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
        httpd.serve_forever()


if __name__ == "__main__":
    main()
