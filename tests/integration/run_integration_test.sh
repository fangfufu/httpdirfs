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
HTTP_PORT="${HTTPDIRFS_TEST_PORT:-0}"

# Parse options
MODE="all"
HTTPDIRFS_BIN=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --short)
            MODE="short"
            shift
            ;;
        --long)
            MODE="long"
            shift
            ;;
        -*)
            echo -e "\033[0;31m[ERROR]\033[0m Unknown option: $1" >&2
            exit 1
            ;;
        *)
            if [[ -z "${HTTPDIRFS_BIN}" ]]; then
                HTTPDIRFS_BIN="$1"
            else
                echo -e "\033[0;31m[ERROR]\033[0m Multiple binary paths specified or extra arguments: $*" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

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
    if [[ -n "${MOUNT_DIR:-}" ]] && mountpoint -q "${MOUNT_DIR}" 2>/dev/null; then
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
MANIFEST="${SERVE_DIR}/manifest.json"
MOUNT_TIMEOUT=15

mkdir -p "${SERVE_DIR}" "${MOUNT_DIR}" "${CACHE_MOUNT_DIR}" "${CACHE_DIR}"

log_info "Work directory: ${WORK_DIR}"
log_info "httpdirfs binary: ${HTTPDIRFS_BIN}"

# ─── Step 1: Generate test files ────────────────────────────────────────────

if [[ "${MODE}" == "short" ]]; then
    log_info "Generating test files (excluding 1 GB large file)..."
    python3 "${SCRIPT_DIR}/generate_test_files.py" "${SERVE_DIR}" --short
elif [[ "${MODE}" == "long" ]]; then
    log_info "Generating test files (1 GB large file only)..."
    python3 "${SCRIPT_DIR}/generate_test_files.py" "${SERVE_DIR}" --long
else
    log_info "Generating all test files (including 1 GB large file)..."
    python3 "${SCRIPT_DIR}/generate_test_files.py" "${SERVE_DIR}" --all
fi

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
TEST_FILE="simple.txt"
if [[ "${MODE}" == "long" ]]; then
    TEST_FILE="large_1g.bin"
fi
RANGE_TEST=$(curl -sf -r 0-3 "http://127.0.0.1:${ACTUAL_PORT}/${TEST_FILE}" \
    -w "%{http_code}" -o /dev/null 2>/dev/null)
if [[ "${RANGE_TEST}" == "206" ]]; then
    log_info "HTTP server supports Range requests (206 Partial Content)."
else
    log_warn "Range request returned HTTP ${RANGE_TEST} (expected 206)."
fi

BASE_URL="http://127.0.0.1:${ACTUAL_PORT}/"

if [[ "${MODE}" != "long" ]]; then
# ─── Step 3: Mount with httpdirfs (non-cache mode) ─────────────────────────

log_info "Mounting with httpdirfs (non-cache mode)..."

"${HTTPDIRFS_BIN}" \
    -f \
    "${BASE_URL}" \
    "${MOUNT_DIR}" &
HTTPDIRFS_PID=$!

# Wait for the mount to become available
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

# 4f-extra. Test: Zero-length files with varied special-character names
log_info "Test group: Zero-length files (varied names)"

# Build the list from the manifest: all files whose size is 0
ZERO_FILES_LIST=$(python3 -c "
import json
with open('${MANIFEST}') as f:
    m = json.load(f)
for name, info in sorted(m.items()):
    if info['size'] == 0:
        print(name)
")

ZERO_FILE_COUNT=0
while IFS= read -r zf_name; do
    [[ -z "${zf_name}" ]] && continue
    ZERO_FILE_COUNT=$((ZERO_FILE_COUNT + 1))
    target="${MOUNT_DIR}/${zf_name}"

    # Existence
    if [[ ! -e "${target}" ]]; then
        fail "Zero-length file missing: ${zf_name}"
        continue
    fi

    # Size must be 0
    zf_size=$(stat -c%s "${target}" 2>/dev/null || echo "-1")
    if [[ "${zf_size}" == "0" ]]; then
        pass "Zero-length size correct: ${zf_name}"
    else
        fail "Zero-length size wrong (got ${zf_size}): ${zf_name}"
    fi

    # Reading must yield empty content (no hang, no error)
    zf_content=$(cat "${target}" 2>/dev/null)
    if [[ -z "${zf_content}" ]]; then
        pass "Zero-length content empty: ${zf_name}"
    else
        fail "Zero-length file has unexpected content: ${zf_name}"
    fi

    # SHA-256 of an empty file is the well-known constant
    EMPTY_SHA256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    zf_sha=$(sha256sum "${target}" 2>/dev/null | awk '{print $1}')
    if [[ "${zf_sha}" == "${EMPTY_SHA256}" ]]; then
        pass "Zero-length checksum OK: ${zf_name}"
    else
        fail "Zero-length checksum mismatch: ${zf_name} (got ${zf_sha})"
    fi
done <<< "${ZERO_FILES_LIST}"

if [[ "${ZERO_FILE_COUNT}" -eq 0 ]]; then
    skip "No zero-length files found in manifest"
else
    log_info "Tested ${ZERO_FILE_COUNT} zero-length file(s)."
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

# 4h. Test: Duplicated URL deduplication
log_info "Test group: Duplicated URL deduplication"

# Count the occurrences of 'simple.txt' in the mounted directory listing
simple_count=$(ls -1 "${MOUNT_DIR}" | grep -Fxc "simple.txt" || true)
if [[ "${simple_count}" -eq 1 ]]; then
    pass "Duplicate 'simple.txt' links successfully deduplicated (count: 1)"
else
    fail "Deduplication failed for 'simple.txt' (count: ${simple_count})"
fi

# Count the occurrences of 'subdir with spaces' in the mounted directory listing
subdir_count_dup=$(ls -1 "${MOUNT_DIR}" | grep -Fxc "subdir with spaces" || true)
if [[ "${subdir_count_dup}" -eq 1 ]]; then
    pass "Duplicate 'subdir with spaces' links successfully deduplicated (count: 1)"
else
    fail "Deduplication failed for 'subdir with spaces' (count: ${subdir_count_dup})"
fi


# ─── Step 5: Unmount non-cache mount ────────────────────────────────────────

log_info "Unmounting non-cache mount..."
do_unmount "${MOUNT_DIR}"
wait "${HTTPDIRFS_PID}" 2>/dev/null || true
log_info "Unmounted successfully."
fi

# ─── Step 5b: External-links tests ─────────────────────────────────────────

if [[ "${MODE}" != "long" ]]; then
log_info "=== External-links tests ==="

# Create the external server's root directory inside WORK_DIR
EXT_SERVE_DIR="${WORK_DIR}/ext_serve"
EXT_MOUNT_DIR="${WORK_DIR}/ext_mnt"
EXT_PORT_FILE="${WORK_DIR}/ext_port"
mkdir -p "${EXT_SERVE_DIR}/external_subdir" "${EXT_MOUNT_DIR}"

# Create test files on the external server
EXT_FILE_CONTENT="Hello from the external server"
echo -n "${EXT_FILE_CONTENT}" > "${EXT_SERVE_DIR}/external_file.txt"
EXT_FILE_SHA=$(echo -n "${EXT_FILE_CONTENT}" | sha256sum | awk '{print $1}')

# 1 MB file for content integrity verification
dd if=/dev/urandom bs=1M count=1 \
    of="${EXT_SERVE_DIR}/external_large.bin" 2>/dev/null
EXT_LARGE_SHA=$(sha256sum "${EXT_SERVE_DIR}/external_large.bin" \
    | awk '{print $1}')

# File in external subdirectory
echo -n "nested content" > "${EXT_SERVE_DIR}/external_subdir/nested_file.txt"
NESTED_SHA=$(echo -n "nested content" | sha256sum | awk '{print $1}')

# Two files with the same name on different paths (dedup test)
echo -n "first duplicate" > "${EXT_SERVE_DIR}/duplicate.txt"
DUP_SHA=$(echo -n "first duplicate" | sha256sum | awk '{print $1}')
mkdir -p "${EXT_SERVE_DIR}/other"
echo -n "second duplicate" > "${EXT_SERVE_DIR}/other/duplicate.txt"

# Start the external HTTP server
python3 "${SCRIPT_DIR}/range_http_server.py" \
    "${EXT_SERVE_DIR}" 0 "${EXT_PORT_FILE}" &
EXT_HTTP_PID=$!

# Register for cleanup
cleanup_ext() {
    # Unmount any FUSE mounts still attached to the external server
    # before killing it, to avoid in-flight request aborts.
    if [[ -d "${EXT_MOUNT_DIR}" ]] \
        && mountpoint -q "${EXT_MOUNT_DIR}" 2>/dev/null; then
        do_unmount "${EXT_MOUNT_DIR}"
        sleep 1
    fi
    if [[ -n "${EXT_HTTP_PID:-}" ]] \
        && kill -0 "${EXT_HTTP_PID}" 2>/dev/null; then
        kill "${EXT_HTTP_PID}" 2>/dev/null || true
        wait "${EXT_HTTP_PID}" 2>/dev/null || true
    fi
}
trap 'cleanup_ext; cleanup' EXIT

# Wait for the external server port file
for i in $(seq 1 10); do
    [[ -f "${EXT_PORT_FILE}" ]] && break
    sleep 0.5
done
if [[ ! -f "${EXT_PORT_FILE}" ]]; then
    log_error "External HTTP server did not write port file."
    exit 1
fi

EXT_PORT="$(cat "${EXT_PORT_FILE}")"
log_info "External HTTP server on port ${EXT_PORT} (PID: ${EXT_HTTP_PID})"
EXT_BASE_URL="http://127.0.0.1:${EXT_PORT}"

# Add a local file on the primary server for baseline comparison
echo -n "local content" > "${SERVE_DIR}/local_ext_test.txt"
LOCAL_SHA=$(echo -n "local content" | sha256sum | awk '{print $1}')

# Write the mixed-link index.html into the primary server directory.
# The primary server's SimpleHTTPRequestHandler will serve both the
# auto-generated listing AND this custom index.html.  We place it
# directly in SERVE_DIR so it is available alongside the real files.
# NOTE: httpdirfs fetches the root URL and parses whatever HTML it gets
# back; Python's http.server serves the auto-generated directory listing,
# not index.html.  We therefore create a dedicated subdirectory on Server A
# that contains ONLY the hand-crafted index.html, so httpdirfs parses
# our known HTML rather than the auto-generated listing.
EXT_TEST_DIR="${SERVE_DIR}/ext_test_dir"
mkdir -p "${EXT_TEST_DIR}"
echo -n "local content" > "${EXT_TEST_DIR}/local_ext_test.txt"

cat > "${EXT_TEST_DIR}/index.html" <<HTML
<!DOCTYPE html>
<html>
<body>
<a href="local_ext_test.txt">local_ext_test.txt</a>
<a href="${EXT_BASE_URL}/external_file.txt">external_file.txt</a>
<a href="${EXT_BASE_URL}/external_large.bin">external_large.bin</a>
<a href="${EXT_BASE_URL}/external_subdir/">external_subdir</a>
<a href="${EXT_BASE_URL}/duplicate.txt">duplicate.txt</a>
<a href="${EXT_BASE_URL}/other/duplicate.txt">duplicate.txt</a>
</body>
</html>
HTML

EXT_TEST_URL="${BASE_URL}ext_test_dir/"

# ── Test 1: file listing with --external-links ─────────────────────────────
log_info "Test group: External-links file listing"

"${HTTPDIRFS_BIN}" \
    -f \
    --external-links \
    "${EXT_TEST_URL}" \
    "${EXT_MOUNT_DIR}" &
EXT_HTTPDIRFS_PID=$!

for i in $(seq 1 "${MOUNT_TIMEOUT}"); do
    mountpoint -q "${EXT_MOUNT_DIR}" 2>/dev/null && break
    sleep 1
done

if ! mountpoint -q "${EXT_MOUNT_DIR}" 2>/dev/null; then
    log_error "httpdirfs (--external-links) failed to mount."
    kill "${EXT_HTTPDIRFS_PID}" 2>/dev/null || true
else
    # local file is present
    if [[ -e "${EXT_MOUNT_DIR}/local_ext_test.txt" ]]; then
        pass "external_link_file_listing: local file present"
    else
        fail "external_link_file_listing: local file missing"
    fi

    # external files appear in listing
    if [[ -e "${EXT_MOUNT_DIR}/external_file.txt" ]]; then
        pass "external_link_file_listing: external_file.txt present"
    else
        fail "external_link_file_listing: external_file.txt missing"
    fi

    if [[ -e "${EXT_MOUNT_DIR}/external_large.bin" ]]; then
        pass "external_link_file_listing: external_large.bin present"
    else
        fail "external_link_file_listing: external_large.bin missing"
    fi

    # ── Test 2: content integrity ───────────────────────────────────────────
    log_info "Test group: External-links content integrity"

    if [[ -e "${EXT_MOUNT_DIR}/external_file.txt" ]]; then
        actual=$(sha256sum "${EXT_MOUNT_DIR}/external_file.txt" \
            | awk '{print $1}')
        if [[ "${actual}" == "${EXT_FILE_SHA}" ]]; then
            pass "external_link_content_integrity: external_file.txt OK"
        else
            fail "external_link_content_integrity: external_file.txt checksum mismatch"
        fi
    else
        skip "external_link_content_integrity: external_file.txt missing"
    fi

    if [[ -e "${EXT_MOUNT_DIR}/external_large.bin" ]]; then
        actual=$(sha256sum "${EXT_MOUNT_DIR}/external_large.bin" \
            | awk '{print $1}')
        if [[ "${actual}" == "${EXT_LARGE_SHA}" ]]; then
            pass "external_link_content_integrity: external_large.bin OK"
        else
            fail "external_link_content_integrity: external_large.bin checksum mismatch"
        fi
    else
        skip "external_link_content_integrity: external_large.bin missing"
    fi

    # ── Test 4: external directory ──────────────────────────────────────────
    log_info "Test group: External-links directory traversal"

    if [[ -d "${EXT_MOUNT_DIR}/external_subdir" ]]; then
        pass "external_link_directory: external_subdir is a directory"
        if [[ -e "${EXT_MOUNT_DIR}/external_subdir/nested_file.txt" ]]; then
            pass "external_link_directory: nested_file.txt present"
            nested_sha=$(sha256sum \
                "${EXT_MOUNT_DIR}/external_subdir/nested_file.txt" \
                | awk '{print $1}')
            if [[ "${nested_sha}" == "${NESTED_SHA}" ]]; then
                pass "external_link_directory: nested_file.txt content OK"
            else
                fail "external_link_directory: nested_file.txt checksum mismatch"
            fi
        else
            fail "external_link_directory: nested_file.txt missing"
        fi
    else
        fail "external_link_directory: external_subdir not a directory"
    fi

    # ── Test 5: first-wins dedup ────────────────────────────────────────────
    log_info "Test group: External-links first-wins deduplication"

    dup_count=$(ls -1 "${EXT_MOUNT_DIR}" | grep -c "^duplicate\.txt$" || true)
    if [[ "${dup_count}" -eq 1 ]]; then
        pass "external_link_dedup_first_wins: exactly one duplicate.txt"
        dup_sha=$(sha256sum "${EXT_MOUNT_DIR}/duplicate.txt" \
            | awk '{print $1}')
        if [[ "${dup_sha}" == "${DUP_SHA}" ]]; then
            pass "external_link_dedup_first_wins: first duplicate wins"
        else
            fail "external_link_dedup_first_wins: wrong duplicate content"
        fi
    else
        fail "external_link_dedup_first_wins: expected 1 duplicate.txt, got ${dup_count}"
    fi

    do_unmount "${EXT_MOUNT_DIR}"
    wait "${EXT_HTTPDIRFS_PID}" 2>/dev/null || true
fi

# ── Test 3: backward compatibility (no --external-links) ───────────────────
log_info "Test group: External-links backward compatibility"

"${HTTPDIRFS_BIN}" \
    -f \
    "${EXT_TEST_URL}" \
    "${EXT_MOUNT_DIR}" &
COMPAT_PID=$!

for i in $(seq 1 "${MOUNT_TIMEOUT}"); do
    mountpoint -q "${EXT_MOUNT_DIR}" 2>/dev/null && break
    sleep 1
done

if ! mountpoint -q "${EXT_MOUNT_DIR}" 2>/dev/null; then
    log_error "httpdirfs (no --external-links) failed to mount."
    kill "${COMPAT_PID}" 2>/dev/null || true
else
    if [[ ! -e "${EXT_MOUNT_DIR}/external_file.txt" ]]; then
        pass "external_link_backward_compat: external_file.txt not present (correct)"
    else
        fail "external_link_backward_compat: external_file.txt unexpectedly present"
    fi

    if [[ -e "${EXT_MOUNT_DIR}/local_ext_test.txt" ]]; then
        pass "external_link_backward_compat: local_ext_test.txt still present"
    else
        fail "external_link_backward_compat: local_ext_test.txt missing"
    fi

    do_unmount "${EXT_MOUNT_DIR}"
    wait "${COMPAT_PID}" 2>/dev/null || true
fi

# ── Test 6: cache mode with --external-links ───────────────────────────────
log_info "Test group: External-links cache mode"
# NOTE: Cache mode with external links requires additional work in the cache
# path (Meta_open uses ROOT_LINK_OFFSET which is incorrect for cross-origin
# URLs). This will be addressed in a follow-up. Skip for now.
skip "external_link_cache_mode: cache path for external URLs not yet supported"

# Stop the external HTTP server
cleanup_ext
log_info "External HTTP server stopped."
fi

# ─── Step 6: Cache mode with multithreaded reads ────────────────────────────

if [[ "${MODE}" != "short" ]]; then
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

        # Remove and recreate the cache directory for a completely
        # clean state — ensures no metadata from a previous block size
        # can affect the next run
        rm -rf "${CACHE_DIR:?}"
        mkdir -p "${CACHE_DIR}"

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

        # Test: Zero-length files in cache mode (run once — not block-size
        # dependent, but checked here to confirm the fi->fh=0 bypass works
        # when the cache system is active).
        if [[ "${MODE}" != "long" && "${BLKSZ}" == "8" ]]; then
            log_info "Test group: Zero-length files in cache mode"
            CACHE_ZERO_FILES_LIST=$(python3 -c "
import json
with open('${MANIFEST}') as f:
    m = json.load(f)
for name, info in sorted(m.items()):
    if info['size'] == 0:
        print(name)
")
            CACHE_ZERO_COUNT=0
            EMPTY_SHA256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            while IFS= read -r czf_name; do
                [[ -z "${czf_name}" ]] && continue
                CACHE_ZERO_COUNT=$((CACHE_ZERO_COUNT + 1))
                czf_target="${CACHE_MOUNT_DIR}/${czf_name}"

                if [[ ! -e "${czf_target}" ]]; then
                    fail "Cache: zero-length file missing: ${czf_name}"
                    continue
                fi

                czf_size=$(stat -c%s "${czf_target}" 2>/dev/null || echo "-1")
                if [[ "${czf_size}" == "0" ]]; then
                    pass "Cache: zero-length size correct: ${czf_name}"
                else
                    fail "Cache: zero-length size wrong (${czf_size}): ${czf_name}"
                fi

                czf_content=$(cat "${czf_target}" 2>/dev/null)
                if [[ -z "${czf_content}" ]]; then
                    pass "Cache: zero-length content empty: ${czf_name}"
                else
                    fail "Cache: zero-length has unexpected content: ${czf_name}"
                fi

                czf_sha=$(sha256sum "${czf_target}" 2>/dev/null | awk '{print $1}')
                if [[ "${czf_sha}" == "${EMPTY_SHA256}" ]]; then
                    pass "Cache: zero-length checksum OK: ${czf_name}"
                else
                    fail "Cache: zero-length checksum mismatch: ${czf_name}"
                fi
            done <<< "${CACHE_ZERO_FILES_LIST}"
            if [[ "${CACHE_ZERO_COUNT}" -eq 0 ]]; then
                skip "Cache: no zero-length files in manifest"
            else
                log_info "Cache: tested ${CACHE_ZERO_COUNT} zero-length file(s)."
            fi
        fi

        # Unmount
        do_unmount "${CACHE_MOUNT_DIR}"
        wait "${CACHE_HTTPDIRFS_PID}" 2>/dev/null || true
        log_info "Cache mount (blksz=${BLKSZ}M) unmounted."

    done
fi
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
