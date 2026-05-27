#!/usr/bin/env python3
"""Generate test files with various filenames for HTTPDirFS integration tests.

Creates a set of files with known content and checksums to verify filesystem
correctness after mounting with httpdirfs. Filenames include
non-alphanumeric characters, spaces, and other edge cases.

File sizes are randomised between 1 KB and 10 MB using a deterministic seed
so that the manifest is reproducible across runs.
"""

import hashlib
import json
import os
import random
import sys

# Fixed seed so that file sizes are reproducible across runs, while still
# being "random" in the sense that they cover a wide range.
RNG_SEED = 20260518

# 64 KB write chunk — balances memory usage and I/O throughput
CHUNK_SIZE = 64 * 1024


def write_deterministic_file(filepath, name, size):
    """Write a file with deterministic content and return its SHA-256.

    Uses a name-derived seed with Python's Mersenne Twister PRNG.
    Streams to disk in 64 KB chunks so even multi-GB files use constant
    memory.
    """
    if size == 0:
        with open(filepath, "wb"):
            pass
        return hashlib.sha256(b"").hexdigest()

    seed_hash = hashlib.sha256(name.encode()).digest()
    file_rng = random.Random(int.from_bytes(seed_hash[:8], "big"))
    h = hashlib.sha256()
    written = 0

    with open(filepath, "wb") as f:
        while written < size:
            n = min(CHUNK_SIZE, size - written)
            chunk = file_rng.randbytes(n)
            f.write(chunk)
            h.update(chunk)
            written += n

    return h.hexdigest()


def random_size(rng):
    """Return a random file size between 1 KB and 10 MB."""
    return rng.randint(1024, 10 * 1024 * 1024)


def format_size(size):
    """Human-readable size string."""
    if size >= 1024 * 1024 * 1024:
        return f"{size / (1024**3):.1f} GB"
    if size >= 1024 * 1024:
        return f"{size / (1024**2):.1f} MB"
    if size >= 1024:
        return f"{size / 1024:.1f} KB"
    return f"{size} bytes"


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <output_directory> [--short | --long | --all]",
              file=sys.stderr)
        sys.exit(1)

    output_dir = sys.argv[1]

    # Parse options
    is_short = False
    is_large = False

    if "--short" in sys.argv:
        is_short = True
    elif "--long" in sys.argv:
        is_large = True
    elif "--all" in sys.argv:
        is_short = True
        is_large = True

    os.makedirs(output_dir, exist_ok=True)

    rng = random.Random(RNG_SEED)

    test_files = []
    if is_short:
        # Filenames that exercise interesting edge cases.  Each file gets a
        # random size between 1 KB and 10 MB.
        filenames = [
            # Basic ASCII filenames
            "simple.txt",
            "UPPERCASE.DAT",

            # Filenames with spaces
            "file with spaces.txt",
            "multiple   spaces   here.bin",

            # Non-alphanumeric characters that are valid in URLs
            "file-with-dashes.txt",
            "file_with_underscores.txt",
            "file.multiple.dots.txt",
            "file~tilde.txt",

            # Percent-encoded characters in HTTP (common edge cases)
            "parens(1).txt",
            "brackets[2].txt",
            "curly{3}.txt",
            "hash#tag.txt",
            "at@sign.txt",
            "plus+plus.txt",
            "equals=value.txt",
            "comma,separated.txt",
            "semi;colon.txt",
            "exclaim!mark.txt",
            "single'quote.txt",

            # Mixed case and numbers
            "CamelCase123.txt",
            "MiXeD_cAsE-456.dat",

            # Longer filenames
            "a" * 200 + ".txt",
        ]

        # Build (filename, size) pairs with random sizes
        test_files = [(name, random_size(rng)) for name in filenames]

        # Keep a few deterministic edge-case sizes
        test_files.append(("empty_file.txt", 0))
        test_files.append(("tiny.txt", 1))

        # Zero-length files with varied names to exercise the fi->fh=0 bypass
        # path through every special-character category.
        zero_length_names = [
            "zero_basic.txt",
            "zero file with spaces.txt",
            "zero-dashes.bin",
            "zero_underscores.bin",
            "zero.multiple.dots.bin",
            "zero(parens).txt",
            "zero[brackets].txt",
            "zero@at.txt",
            "zero" + "a" * 50 + ".txt",   # moderately long name
        ]
        for name in zero_length_names:
            test_files.append((name, 0))

    # Add the 1 GB file for cache system testing when is_large is given
    if is_large:
        test_files.append(("large_1g.bin", 1024 * 1024 * 1024))

    manifest = {}

    for filename, size in test_files:
        filepath = os.path.join(output_dir, filename)
        checksum = write_deterministic_file(filepath, filename, size)

        manifest[filename] = {
            "size": size,
            "sha256": checksum,
        }
        print(f"  Created: {filename} ({format_size(size)},"
              f" sha256={checksum[:16]}...)")

    if is_short:
        # Also create a subdirectory with files (random sizes too)
        subdir = os.path.join(output_dir, "subdir with spaces")
        os.makedirs(subdir, exist_ok=True)

        subdir_filenames = [
            "nested file.txt",
            "deep-data.bin",
        ]

        for filename in subdir_filenames:
            size = random_size(rng)
            filepath = os.path.join(subdir, filename)
            seed_name = f"subdir/{filename}"
            checksum = write_deterministic_file(filepath, seed_name, size)

            relative = os.path.join("subdir with spaces", filename)
            manifest[relative] = {
                "size": size,
                "sha256": checksum,
            }
            print(f"  Created: {relative} ({format_size(size)},"
                  f" sha256={checksum[:16]}...)")

    # Write manifest
    manifest_path = os.path.join(output_dir, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print(f"\nManifest written to {manifest_path}")
    print(f"Total files: {len(manifest)}")


if __name__ == "__main__":
    main()
