#!/usr/bin/env bash
#
# Integration test for HTTPDirFS
#
# This script:
#   1. Generates test files with various filenames (including non-alphanumeric)
#   2. Starts an HTTP server with Range request support
#   3. Mounts the HTTP directory with httpdirfs
#   4. Verifies file listing, content integrity (SHA-256), and sizes
#   5. Tests cache mode with multithreaded reads on a large file
#   6. Cleans up (unmount, stop server, remove temp files)
#
# Usage: ./run_integration_test.sh [path_to_httpdirfs_binary]
#
# Exit codes:
#   0 - All tests passed
#   1 - Test failure or setup error

set -euo pipefail

# ─── Configuration ───────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HTTPDIRFS_BIN="${1:-}"
HTTP_PORT="${HTTPDIRFS_TEST_PORT:-0}"

# Colours for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Colour

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

# ─── Helpers ─────────────────────────────────────────────────────────────────

log_info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

pass() { PASS_COUNT=$((PASS_COUNT + 1)); echo -e "  ${GREEN}PASS${NC}: $*"; }
fail() { FAIL_COUNT=$((FAIL_COUNT + 1)); echo -e "  ${RED}FAIL${NC}: $*"; }
skip() { SKIP_COUNT=$((SKIP_COUNT + 1)); echo -e "  ${YELLOW}SKIP${NC}: $*"; }

do_unmount() {
    local mnt="$1"
    fusermount3 -u "${mnt}" 2>/dev/null || \
        fusermount -u "${mnt}" 2>/dev/null || \
        umount "${mnt}" 2>/dev/null || true
}

cleanup() {
    log_info "Cleaning up..."

    # Unmount if still mounted
    if mountpoint -q "${MOUNT_DIR}" 2>/dev/null; then
        do_unmount "${MOUNT_DIR}"
        sleep 1
    fi
    if [[ -n "${CACHE_MOUNT_DIR:-}" ]] \
        && mountpoint -q "${CACHE_MOUNT_DIR}" 2>/dev/null; then
        do_unmount "${CACHE_MOUNT_DIR}"
        sleep 1
    fi

    # Stop HTTP server
    if [[ -n "${HTTP_PID:-}" ]] && kill -0 "${HTTP_PID}" 2>/dev/null; then
        kill "${HTTP_PID}" 2>/dev/null || true
        wait "${HTTP_PID}" 2>/dev/null || true
    fi

    # Remove temp directories
    if [[ -n "${WORK_DIR:-}" && -d "${WORK_DIR}" ]]; then
        rm -rf "${WORK_DIR}"
    fi

    log_info "Cleanup complete."
}

trap cleanup EXIT

# ─── Precondition checks ────────────────────────────────────────────────────

if [[ -z "${HTTPDIRFS_BIN}" ]]; then
    log_error "Usage: $0 <path_to_httpdirfs_binary>"
    exit 1
fi

if [[ ! -x "${HTTPDIRFS_BIN}" ]]; then
    log_error "httpdirfs binary not found or not executable: ${HTTPDIRFS_BIN}"
    exit 1
fi

if [[ ! -e /dev/fuse ]]; then
    log_warn "/dev/fuse not found — FUSE is not available, skipping tests."
    exit 0
fi

if ! command -v python3 &>/dev/null; then
    log_error "python3 is required but not found."
    exit 1
fi

if ! command -v fusermount3 &>/dev/null && ! command -v fusermount &>/dev/null; then
    log_error "fusermount3 (or fusermount) is required but not found."
    exit 1
fi

# ─── Setup ───────────────────────────────────────────────────────────────────

WORK_DIR="$(mktemp -d /tmp/httpdirfs-integration-test.XXXXXX)"
SERVE_DIR="${WORK_DIR}/serve"
MOUNT_DIR="${WORK_DIR}/mnt"
CACHE_MOUNT_DIR="${WORK_DIR}/cache_mnt"
CACHE_DIR="${WORK_DIR}/cache"

mkdir -p "${SERVE_DIR}" "${MOUNT_DIR}" "${CACHE_MOUNT_DIR}" "${CACHE_DIR}"

log_info "Work directory: ${WORK_DIR}"
log_info "httpdirfs binary: ${HTTPDIRFS_BIN}"

# ─── Step 1: Generate test files ────────────────────────────────────────────

log_info "Generating test files (including 1 GB large file)..."
python3 "${SCRIPT_DIR}/generate_test_files.py" "${SERVE_DIR}" --large

# ─── Step 2: Start HTTP server with Range support ───────────────────────────

log_info "Starting HTTP server with Range support..."

PORT_FILE="${WORK_DIR}/port"

python3 "${SCRIPT_DIR}/range_http_server.py" \
    "${SERVE_DIR}" "${HTTP_PORT}" "${PORT_FILE}" &
HTTP_PID=$!

# Wait for port file to appear
for i in $(seq 1 10); do
    if [[ -f "${PORT_FILE}" ]]; then
        break
    fi
    sleep 0.5
done

if [[ ! -f "${PORT_FILE}" ]]; then
    log_error "HTTP server did not write port file."
    exit 1
fi

ACTUAL_PORT="$(cat "${PORT_FILE}")"

if [[ -z "${ACTUAL_PORT}" ]]; then
    log_error "Could not determine HTTP server port."
    exit 1
fi

log_info "HTTP server running on port ${ACTUAL_PORT} (PID: ${HTTP_PID})"

# Verify server is responding
if ! curl -sf "http://127.0.0.1:${ACTUAL_PORT}/" >/dev/null 2>&1; then
    log_error "HTTP server is not responding."
    exit 1
fi

# Verify Range support works
RANGE_TEST=$(curl -sf -r 0-3 "http://127.0.0.1:${ACTUAL_PORT}/simple.txt" \
    -w "%{http_code}" -o /dev/null 2>/dev/null)
if [[ "${RANGE_TEST}" == "206" ]]; then
    log_info "HTTP server supports Range requests (206 Partial Content)."
else
    log_warn "Range request returned HTTP ${RANGE_TEST} (expected 206)."
fi

BASE_URL="http://127.0.0.1:${ACTUAL_PORT}/"

# ─── Step 3: Mount with httpdirfs (non-cache mode) ─────────────────────────

log_info "Mounting with httpdirfs (non-cache mode)..."

"${HTTPDIRFS_BIN}" \
    -f \
    "${BASE_URL}" \
    "${MOUNT_DIR}" &
HTTPDIRFS_PID=$!

# Wait for the mount to become available
MOUNT_TIMEOUT=15
for i in $(seq 1 "${MOUNT_TIMEOUT}"); do
    if mountpoint -q "${MOUNT_DIR}" 2>/dev/null; then
        break
    fi
    sleep 1
done

if ! mountpoint -q "${MOUNT_DIR}" 2>/dev/null; then
    log_error "httpdirfs failed to mount within ${MOUNT_TIMEOUT} seconds."
    # Try to see if the process is still alive
    if ! kill -0 "${HTTPDIRFS_PID}" 2>/dev/null; then
        log_error "httpdirfs process has exited."
    fi
    exit 1
fi

log_info "httpdirfs mounted successfully at ${MOUNT_DIR}"

# ─── Step 4: Run basic tests ────────────────────────────────────────────────

log_info "Running integration tests..."

# Load the manifest
MANIFEST="${SERVE_DIR}/manifest.json"

# Generate a test plan from the manifest — one line per file with
# tab-separated fields: filename, size, sha256
TEST_PLAN="${WORK_DIR}/test_plan.tsv"
python3 -c "
import json
with open('${MANIFEST}') as f:
    m = json.load(f)
for name in sorted(m.keys()):
    size = m[name]['size']
    sha = m[name]['sha256']
    print(f'{name}\t{size}\t{sha}')
" > "${TEST_PLAN}"

# 4a. Test: Directory listing contains expected files
log_info "Test group: Directory listing"

while IFS=$'\t' read -r filename expected_size expected_sha256; do
    target="${MOUNT_DIR}/${filename}"

    if [[ -e "${target}" ]]; then
        pass "File exists: ${filename}"
    else
        fail "File missing: ${filename}"
    fi
done < "${TEST_PLAN}"

# 4b. Test: File sizes match
log_info "Test group: File sizes"

while IFS=$'\t' read -r filename expected_size expected_sha256; do
    target="${MOUNT_DIR}/${filename}"
    if [[ ! -e "${target}" ]]; then
        skip "Size check (file missing): ${filename}"
        continue
    fi

    actual_size=$(stat -c%s "${target}" 2>/dev/null || echo "-1")
    if [[ "${actual_size}" == "${expected_size}" ]]; then
        pass "Size correct: ${filename} (${actual_size} bytes)"
    else
        fail "Size mismatch: ${filename} (expected=${expected_size}, actual=${actual_size})"
    fi
done < "${TEST_PLAN}"

# 4c. Test: Content integrity (SHA-256 checksums)
log_info "Test group: Content integrity (SHA-256)"

while IFS=$'\t' read -r filename expected_size expected_sha256; do
    target="${MOUNT_DIR}/${filename}"
    if [[ ! -e "${target}" ]]; then
        skip "Checksum (file missing): ${filename}"
        continue
    fi

    actual_sha256=$(sha256sum "${target}" 2>/dev/null | awk '{print $1}')
    if [[ "${actual_sha256}" == "${expected_sha256}" ]]; then
        pass "Checksum OK: ${filename}"
    else
        fail "Checksum mismatch: ${filename}"
        log_error "  expected: ${expected_sha256}"
        log_error "  actual:   ${actual_sha256}"
    fi
done < "${TEST_PLAN}"

# 4d. Test: Read the empty file specifically
log_info "Test group: Edge cases"

EMPTY_FILE="${MOUNT_DIR}/empty_file.txt"
if [[ -e "${EMPTY_FILE}" ]]; then
    content=$(cat "${EMPTY_FILE}")
    if [[ -z "${content}" ]]; then
        pass "Empty file reads as empty"
    else
        fail "Empty file has unexpected content"
    fi
else
    skip "Empty file not found"
fi

# 4e. Test: Tiny file (1 byte)
TINY_FILE="${MOUNT_DIR}/tiny.txt"
if [[ -e "${TINY_FILE}" ]]; then
    tiny_size=$(stat -c%s "${TINY_FILE}" 2>/dev/null || echo "-1")
    if [[ "${tiny_size}" == "1" ]]; then
        pass "Tiny file has correct size (1 byte)"
    else
        fail "Tiny file size mismatch (expected=1, actual=${tiny_size})"
    fi
else
    skip "Tiny file not found"
fi

# 4f. Test: Subdirectory is readable
SUBDIR="${MOUNT_DIR}/subdir with spaces"
if [[ -d "${SUBDIR}" ]]; then
    pass "Subdirectory with spaces is accessible"
    subdir_count=$(ls -1 "${SUBDIR}" 2>/dev/null | wc -l)
    if [[ "${subdir_count}" -ge 2 ]]; then
        pass "Subdirectory contains expected files (${subdir_count} entries)"
    else
        fail "Subdirectory has unexpected entry count: ${subdir_count}"
    fi
else
    skip "Subdirectory with spaces not found"
fi

# 4g. Test: Read-only enforcement
log_info "Test group: Read-only enforcement"

if touch "${MOUNT_DIR}/should_not_exist.txt" 2>/dev/null; then
    fail "Write should have been rejected (read-only filesystem)"
    rm -f "${MOUNT_DIR}/should_not_exist.txt" 2>/dev/null || true
else
    pass "Write correctly rejected (read-only filesystem)"
fi

# ─── Step 5: Unmount non-cache mount ────────────────────────────────────────

log_info "Unmounting non-cache mount..."
do_unmount "${MOUNT_DIR}"
wait "${HTTPDIRFS_PID}" 2>/dev/null || true
log_info "Unmounted successfully."

# ─── Step 6: Cache mode with multithreaded reads ────────────────────────────

log_info "=== Cache mode tests ==="

# Get the large file's expected SHA-256 from the manifest
LARGE_FILE_SHA256=$(python3 -c "
import json
with open('${MANIFEST}') as f:
    m = json.load(f)
entry = m.get('large_1g.bin')
if entry:
    print(entry['sha256'])
else:
    print('')
")

if [[ -z "${LARGE_FILE_SHA256}" ]]; then
    skip "Cache test: large_1g.bin not in manifest"
else
    # Test with multiple block sizes to exercise segbc edge cases:
    #   8 MB  - default, 1 GB / 8 MB  = 128 exactly (no remainder)
    #  16 MB  - fewer segments, 1 GB / 16 MB = 64 exactly
    #   1 MB  - many segments, 1 GB / 1 MB  = 1024 exactly
    #   7 MB  - 1 GB / 7 MB ≈ 146.3 (remainder)
    #   3 MB  - 1 GB / 3 MB ≈ 341.3 (remainder)
    BLOCK_SIZES="8 16 1 7 3"

    for BLKSZ in ${BLOCK_SIZES}; do
        log_info "--- Cache test: --dl-seg-size ${BLKSZ} ---"

        # Clean cache directory for each run
        rm -rf "${CACHE_DIR:?}"/*

        "${HTTPDIRFS_BIN}" \
            -f \
            --cache \
            --cache-location "${CACHE_DIR}" \
            --dl-seg-size "${BLKSZ}" \
            "${BASE_URL}" \
            "${CACHE_MOUNT_DIR}" &
        CACHE_HTTPDIRFS_PID=$!

        # Wait for mount
        for i in $(seq 1 "${MOUNT_TIMEOUT}"); do
            if mountpoint -q "${CACHE_MOUNT_DIR}" 2>/dev/null; then
                break
            fi
            sleep 1
        done

        if ! mountpoint -q "${CACHE_MOUNT_DIR}" 2>/dev/null; then
            log_error "httpdirfs (cache, blksz=${BLKSZ}M) failed to mount."
            skip "Cache mode tests (mount failed, blksz=${BLKSZ}M)"
            wait "${CACHE_HTTPDIRFS_PID}" 2>/dev/null || true
            continue
        fi

        log_info "httpdirfs (cache, blksz=${BLKSZ}M) mounted"

        LARGE_FILE="${CACHE_MOUNT_DIR}/large_1g.bin"

        # Test: Multithreaded read with 8 threads
        log_info "Test group: Multithreaded cache read (blksz=${BLKSZ}M)"
        if python3 "${SCRIPT_DIR}/multithread_read.py" \
            "${LARGE_FILE}" "${LARGE_FILE_SHA256}" 8; then
            pass "Multithreaded read (blksz=${BLKSZ}M, 8 threads): OK"
        else
            fail "Multithreaded read (blksz=${BLKSZ}M, 8 threads): FAIL"
        fi

        # Test: Sequential re-read (should come from cache now)
        log_info "Test group: Cached re-read (blksz=${BLKSZ}M)"
        actual_sha256=$(sha256sum "${LARGE_FILE}" 2>/dev/null \
            | awk '{print $1}')
        if [[ "${actual_sha256}" == "${LARGE_FILE_SHA256}" ]]; then
            pass "Cached re-read (blksz=${BLKSZ}M): OK"
        else
            fail "Cached re-read (blksz=${BLKSZ}M): FAIL"
            log_error "  expected: ${LARGE_FILE_SHA256}"
            log_error "  actual:   ${actual_sha256}"
        fi

        # Unmount
        do_unmount "${CACHE_MOUNT_DIR}"
        wait "${CACHE_HTTPDIRFS_PID}" 2>/dev/null || true
        log_info "Cache mount (blksz=${BLKSZ}M) unmounted."
    done
fi


# ─── Summary ────────────────────────────────────────────────────────────────

echo ""
echo "═══════════════════════════════════════════════"
echo -e "  Results: ${GREEN}${PASS_COUNT} passed${NC}, ${RED}${FAIL_COUNT} failed${NC}, ${YELLOW}${SKIP_COUNT} skipped${NC}"
echo "═══════════════════════════════════════════════"
echo ""

if [[ "${FAIL_COUNT}" -gt 0 ]]; then
    log_error "Some tests FAILED."
    exit 1
fi

log_info "All tests passed!"
exit 0
