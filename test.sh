#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOG_BIN="${LOG_BIN:-$SCRIPT_DIR/macos-x86_64/logger}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

export LOG_FILE="$TMP_DIR/test.log"
export LOG_COLOR=0

pass=0
fail=0

check() {
    local description="$1"
    shift

    if "$@"; then
        printf 'PASS: %s\n' "$description"
        ((pass += 1))
    else
        printf 'FAIL: %s\n' "$description" >&2
        ((fail += 1))
    fi
}

contains() {
    local text="$1"
    local file="$2"
    grep -Fq -- "$text" "$file"
}

not_contains() {
    local text="$1"
    local file="$2"
    ! grep -Fq -- "$text" "$file"
}

run_logger() {
    "$LOG_BIN" "$@" >"$TMP_DIR/stdout" 2>"$TMP_DIR/stderr"
}

# ------------------------------------------------------------
# CLI and version
# ------------------------------------------------------------

check "Version command"
run_logger --version

check "Missing arguments return usage error"
bash -c '"$1" >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN"

check "Invalid severity is rejected"
bash -c '"$1" INVALID message >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN"

# ------------------------------------------------------------
# All severities
# ------------------------------------------------------------

: >"$LOG_FILE"

for level in ALWAYS TRACE DEBUG INFO WARN WARNING ERROR CRITICAL FATAL; do
    if [[ "$level" == ALWAYS ]]; then
        LOG_LEVEL=ERROR run_logger "$level" "TEST_$level"
    else
        LOG_LEVEL=TRACE run_logger "$level" "TEST_$level"
    fi

    check "$level appears in file" \
        contains "TEST_$level" "$LOG_FILE"
done

# ------------------------------------------------------------
# Default filtering: INFO and above
# ------------------------------------------------------------

: >"$LOG_FILE"

LOG_LEVEL=INFO run_logger TRACE FILTER_TRACE
check "TRACE filtered at INFO threshold"
not_contains FILTER_TRACE "$LOG_FILE"

LOG_LEVEL=INFO run_logger DEBUG FILTER_DEBUG
check "DEBUG filtered at INFO threshold"
not_contains FILTER_DEBUG "$LOG_FILE"

LOG_LEVEL=INFO run_logger INFO FILTER_INFO
check "INFO included at INFO threshold"
contains FILTER_INFO "$LOG_FILE"

LOG_LEVEL=INFO run_logger ALWAYS FILTER_ALWAYS
check "ALWAYS bypasses threshold"
contains FILTER_ALWAYS "$LOG_FILE"

# ------------------------------------------------------------
# Environment validation
# ------------------------------------------------------------

check "Invalid LOG_LEVEL is rejected"
bash -c 'LOG_LEVEL=BOGUS "$1" INFO test >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN"

# ------------------------------------------------------------
# Output streams
# ------------------------------------------------------------

LOG_LEVEL=TRACE run_logger INFO STREAM_INFO
check "INFO goes to stdout"
contains STREAM_INFO "$TMP_DIR/stdout"

LOG_LEVEL=TRACE run_logger ERROR STREAM_ERROR
check "ERROR goes to stderr"
contains STREAM_ERROR "$TMP_DIR/stderr"

# ------------------------------------------------------------
# Multiline behavior
# ------------------------------------------------------------

: >"$LOG_FILE"
LOG_LEVEL=TRACE run_logger INFO $'MULTI_FIRST\nMULTI_SECOND\nMULTI_THIRD'

check "First multiline segment is written"
contains MULTI_FIRST "$LOG_FILE"

check "Second multiline segment is written"
contains MULTI_SECOND "$LOG_FILE"

check "Third multiline segment is written"
contains MULTI_THIRD "$LOG_FILE"

# ------------------------------------------------------------
# Summary
# ------------------------------------------------------------

printf '\nResults: %d passed, %d failed\n' "$pass" "$fail"

if ((fail > 0)); then
    exit 1
fi
