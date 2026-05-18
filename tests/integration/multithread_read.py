#!/usr/bin/env python3
"""Multithreaded file reader for cache system integration testing.

Reads a file from multiple threads simultaneously, each thread reading
different byte ranges. Verifies each range against expected SHA-256 checksums
computed from the original file content.

Usage:
    python3 multithread_read.py <filepath> <expected_sha256> <num_threads>

Exit code 0 on success, 1 on verification failure.
"""

import hashlib
import os
import sys
import threading


def read_range(filepath, start, length, results, index):
    """Read a byte range from the file and store it in results."""
    try:
        with open(filepath, "rb") as f:
            f.seek(start)
            data = f.read(length)
        results[index] = data
    except Exception as e:
        results[index] = e


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <filepath> <expected_sha256>"
              f" <num_threads>",
              file=sys.stderr)
        sys.exit(1)

    filepath = sys.argv[1]
    expected_sha256 = sys.argv[2]
    num_threads = int(sys.argv[3])

    file_size = os.path.getsize(filepath)
    chunk_size = file_size // num_threads
    remainder = file_size % num_threads

    print(f"  File: {os.path.basename(filepath)}")
    print(f"  Size: {file_size} bytes ({file_size / (1024*1024):.1f} MB)")
    print(f"  Threads: {num_threads}")
    print(f"  Chunk size: {chunk_size} bytes")

    # Launch threads to read different ranges
    threads = []
    results = [None] * num_threads

    offset = 0
    for i in range(num_threads):
        # Last thread gets any remainder bytes
        length = chunk_size + (remainder if i == num_threads - 1 else 0)
        t = threading.Thread(
            target=read_range,
            args=(filepath, offset, length, results, i),
        )
        threads.append(t)
        offset += length

    # Start all threads simultaneously
    for t in threads:
        t.start()

    # Wait for all to complete
    for t in threads:
        t.join()

    # Check for errors
    for i, result in enumerate(results):
        if isinstance(result, Exception):
            print(f"  ERROR: Thread {i} failed: {result}", file=sys.stderr)
            sys.exit(1)
        if result is None:
            print(f"  ERROR: Thread {i} returned no data", file=sys.stderr)
            sys.exit(1)

    # Reassemble and verify
    full_data = b"".join(results)
    actual_sha256 = hashlib.sha256(full_data).hexdigest()

    if actual_sha256 == expected_sha256:
        print(f"  SHA-256 OK: {actual_sha256}")
        sys.exit(0)
    else:
        print(f"  SHA-256 MISMATCH!", file=sys.stderr)
        print(f"    Expected: {expected_sha256}", file=sys.stderr)
        print(f"    Actual:   {actual_sha256}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
