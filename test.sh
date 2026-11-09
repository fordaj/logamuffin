#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# Default to the build.sh output location (build/<mode>/<platform>/).
# Fall back to the run_tests.sh native debug build, then the
# pre-build/<mode> repo-root location for old checkouts.
if [[ -z "${LOG_BIN:-}" ]]; then
    if [[ -x "$SCRIPT_DIR/build/release/macos-x86_64/logamuffin" ]]; then
        LOG_BIN="$SCRIPT_DIR/build/release/macos-x86_64/logamuffin"
    elif [[ -x "$SCRIPT_DIR/build/debug/macos-x86_64/logamuffin" ]]; then
        LOG_BIN="$SCRIPT_DIR/build/debug/macos-x86_64/logamuffin"
    elif [[ -x "$SCRIPT_DIR/build/debug/logamuffin" ]]; then
        LOG_BIN="$SCRIPT_DIR/build/debug/logamuffin"
    else
        LOG_BIN="$SCRIPT_DIR/build/release/macos-x86_64/logamuffin"
    fi
fi
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

contains_bytes() {
    local text="$1"
    local file="$2"
    grep -a -Fq -- "$text" "$file"
}

run_logamuffin() {
    "$LOG_BIN" "$@" >"$TMP_DIR/stdout" 2>"$TMP_DIR/stderr"
}

# ------------------------------------------------------------
# CLI and version
# ------------------------------------------------------------

check "Version command"
run_logamuffin --version

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
        LOG_LEVEL=ERROR run_logamuffin "$level" "TEST_$level"
    else
        LOG_LEVEL=TRACE run_logamuffin "$level" "TEST_$level"
    fi

    check "$level appears in file" \
        contains "TEST_$level" "$LOG_FILE"
done

# ------------------------------------------------------------
# Default filtering: INFO and above
# ------------------------------------------------------------

: >"$LOG_FILE"

LOG_LEVEL=INFO run_logamuffin TRACE FILTER_TRACE
check "TRACE filtered at INFO threshold"
not_contains FILTER_TRACE "$LOG_FILE"

LOG_LEVEL=INFO run_logamuffin DEBUG FILTER_DEBUG
check "DEBUG filtered at INFO threshold"
not_contains FILTER_DEBUG "$LOG_FILE"

LOG_LEVEL=INFO run_logamuffin INFO FILTER_INFO
check "INFO included at INFO threshold"
contains FILTER_INFO "$LOG_FILE"

LOG_LEVEL=INFO run_logamuffin ALWAYS FILTER_ALWAYS
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

LOG_LEVEL=TRACE run_logamuffin INFO STREAM_INFO
check "INFO goes to stdout"
contains STREAM_INFO "$TMP_DIR/stdout"

LOG_LEVEL=TRACE run_logamuffin ERROR STREAM_ERROR
check "ERROR goes to stderr"
contains STREAM_ERROR "$TMP_DIR/stderr"

# ------------------------------------------------------------
# Multiline behavior
# ------------------------------------------------------------

: >"$LOG_FILE"
LOG_LEVEL=TRACE run_logamuffin INFO $'MULTI_FIRST\nMULTI_SECOND\nMULTI_THIRD'

check "First multiline segment is written"
contains MULTI_FIRST "$LOG_FILE"

check "Second multiline segment is written"
contains MULTI_SECOND "$LOG_FILE"

check "Third multiline segment is written"
contains MULTI_THIRD "$LOG_FILE"

# ------------------------------------------------------------
# Help and extensible categories (issue #4)
# ------------------------------------------------------------

check "Help command"
run_logamuffin --help

: >"$LOG_FILE"
LOG_LEVEL=TRACE LOG_LEVELS='NOTICE=25:blue' run_logamuffin NOTICE ENV_NOTICE
check "Custom category from LOG_LEVELS"
contains ENV_NOTICE "$LOG_FILE"

: >"$LOG_FILE"
LOG_LEVEL=INFO LOG_LEVELS='NOTICE=25:blue' run_logamuffin NOTICE ENV_NOTICE_INFO
check "Custom category above INFO threshold"
contains ENV_NOTICE_INFO "$LOG_FILE"

: >"$LOG_FILE"
LOG_LEVEL=INFO LOG_LEVELS='AUDIT=15:blue' run_logamuffin AUDIT ENV_AUDIT_FILTERED
check "Custom category below INFO threshold is filtered"
not_contains ENV_AUDIT_FILTERED "$LOG_FILE"

: >"$LOG_FILE"
LOG_LEVEL=TRACE LOG_CATEGORIES='NOTICE=25:blue' run_logamuffin NOTICE CATEGORIES_ALIAS
check "Custom category from LOG_CATEGORIES"
contains CATEGORIES_ALIAS "$LOG_FILE"

: >"$LOG_FILE"
LOG_LEVEL=WARN LOG_LEVEL_VALUE_INFO=10 run_logamuffin INFO VALUE_ENV_FILTERED
check "LOG_LEVEL_VALUE_INFO below numeric threshold is filtered"
not_contains VALUE_ENV_FILTERED "$LOG_FILE"

: >"$LOG_FILE"
LOG_LEVEL=TRACE LOG_LEVEL_COLOR_INFO=green run_logamuffin INFO COLOR_ENV
check "LOG_LEVEL_COLOR_INFO keeps category"
contains COLOR_ENV "$LOG_FILE"

LOG_COLOR=1 LOG_LEVEL=TRACE LOG_LEVEL_COLOR_INFO=green \
    run_logamuffin INFO COLOR_ENV_BYTES
check "LOG_LEVEL_COLOR_INFO color bytes"
contains_bytes $'\033[0;32m' "$TMP_DIR/stdout"

check "Invalid LOG_LEVELS is rejected"
bash -c 'LOG_LEVELS=BADENTRY "$1" INFO test >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN"

check "Invalid LOG_LEVEL color is rejected"
bash -c 'LOG_LEVEL_COLOR_INFO=notacolor "$1" INFO test >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN"

# ------------------------------------------------------------
# JSON config file (--config, LOG_CONFIG, autodiscovery)
# ------------------------------------------------------------

cat >"$TMP_DIR/custom.json" <<'JSON'
{
  "level": "INFO",
  "levels": {
    "TRACE": {"value": 5, "color": "cyan"},
    "NOTICE": {"value": 25, "color": "blue"},
    "AUDIT": 15
  }
}
JSON

: >"$LOG_FILE"
run_logamuffin --config "$TMP_DIR/custom.json" NOTICE CFG_NOTICE
check "Custom category from --config object form"
contains CFG_NOTICE "$LOG_FILE"

: >"$LOG_FILE"
run_logamuffin --config="$TMP_DIR/custom.json" NOTICE CFG_EQUALS
check "Custom category from --config=<path> form"
contains CFG_EQUALS "$LOG_FILE"

: >"$LOG_FILE"
LOG_LEVEL=INFO run_logamuffin --config "$TMP_DIR/custom.json" AUDIT CFG_AUDIT_FILTERED
check "Config custom value below threshold is filtered"
not_contains CFG_AUDIT_FILTERED "$LOG_FILE"

: >"$LOG_FILE"
LOG_LEVEL=TRACE run_logamuffin --config "$TMP_DIR/custom.json" AUDIT CFG_AUDIT_PASS
check "Config custom value above threshold passes"
contains CFG_AUDIT_PASS "$LOG_FILE"

LOG_COLOR=1 LOG_LEVEL=TRACE run_logamuffin --config "$TMP_DIR/custom.json" NOTICE CFG_COLOR
check "Config color bytes"
contains_bytes $'\033[0;34m' "$TMP_DIR/stdout"

: >"$LOG_FILE"
LOG_CONFIG="$TMP_DIR/custom.json" LOG_LEVEL=TRACE run_logamuffin NOTICE VIA_LOG_CONFIG
check "Custom category from LOG_CONFIG"
contains VIA_LOG_CONFIG "$LOG_FILE"

cat >"$TMP_DIR/array.json" <<'JSON'
{"levels": [{"name": "NOTICE", "value": 25, "color": "blue"}, {"name": "PING", "value": 3, "color": "#ff8800"}]}
JSON

: >"$LOG_FILE"
LOG_LEVEL=0 run_logamuffin --config "$TMP_DIR/array.json" PING ARRAY_PING
check "Custom category from --config array form"
contains ARRAY_PING "$LOG_FILE"

LOG_COLOR=1 LOG_LEVEL=0 run_logamuffin --config "$TMP_DIR/array.json" PING TRUECOLOR
check "Config truecolor hex bytes"
contains_bytes $'\033[38;2;255;136;0m' "$TMP_DIR/stdout"

cat >"$TMP_DIR/threshold.json" <<'JSON'
{"level": "ERROR", "levels": {}}
JSON

: >"$LOG_FILE"
run_logamuffin --config "$TMP_DIR/threshold.json" INFO THRESHOLD_FILTERED
check "Config level threshold filters INFO"
not_contains THRESHOLD_FILTERED "$LOG_FILE"

printf '%s' '{"levels":{"INFO":{"value":20,"color":"red"}}}' >"$TMP_DIR/env.json"
LOG_COLOR=1 LOG_LEVEL=INFO run_logamuffin --config "$TMP_DIR/env.json" INFO FILE_RED
check "Config color applied"
contains_bytes $'\033[0;31m' "$TMP_DIR/stdout"

LOG_COLOR=1 LOG_LEVEL=INFO LOG_LEVEL_COLOR_INFO=green \
    run_logamuffin --config "$TMP_DIR/env.json" INFO ENV_GREEN
check "Environment color overrides config file"
contains_bytes $'\033[0;32m' "$TMP_DIR/stdout"

LOG_COLOR=1 LOG_LEVEL=INFO LOG_LEVELS='INFO=22:blue' \
    run_logamuffin --config "$TMP_DIR/env.json" INFO ENV_LEVELS_BLUE
check "LOG_LEVELS overrides config file color"
contains_bytes $'\033[0;34m' "$TMP_DIR/stdout"

: >"$LOG_FILE"
LOG_LEVEL=TRACE LOG_LEVELS='ALERT=45:red' run_logamuffin ALERT HIGH_STDErr
check "Custom high category goes to stderr"
contains HIGH_STDErr "$TMP_DIR/stderr"

mkdir -p "$TMP_DIR/disc" "$TMP_DIR/empty"
printf '%s' '{"levels":{"NOTICE":{"value":25,"color":"blue"}}}' >"$TMP_DIR/disc/.logamuffin"
( cd "$TMP_DIR/disc" && LOG_FILE="$TMP_DIR/disc.log" LOG_LEVEL=TRACE "$LOG_BIN" NOTICE AUTO_NOTICE >"$TMP_DIR/disc.out" 2>"$TMP_DIR/disc.err" )
check "Autodiscovered config is used"
contains AUTO_NOTICE "$TMP_DIR/disc.log"

( cd "$TMP_DIR/empty" && LOG_FILE="$TMP_DIR/empty.log" LOG_LEVEL=TRACE "$LOG_BIN" INFO EMPTY_INFO >"$TMP_DIR/empty.out" 2>"$TMP_DIR/empty.err" )
check "Missing autodiscovered file uses defaults"
contains EMPTY_INFO "$TMP_DIR/empty.log"

printf '%s' '{"levels":{"NOTICE":{"value":5,"color":"red"}}}' >"$TMP_DIR/disc/.logamuffin"
printf '%s' '{"levels":{"NOTICE":{"value":25,"color":"blue"}}}' >"$TMP_DIR/explicit.json"
: >"$TMP_DIR/prec.log"
( cd "$TMP_DIR/disc" && LOG_FILE="$TMP_DIR/prec.log" LOG_LEVEL=INFO "$LOG_BIN" NOTICE AUTO_FILTERED >"$TMP_DIR/prec1.out" 2>&1 )
check "Autodiscovered low value is filtered"
not_contains AUTO_FILTERED "$TMP_DIR/prec.log"
( cd "$TMP_DIR/disc" && LOG_FILE="$TMP_DIR/prec.log" LOG_LEVEL=INFO "$LOG_BIN" --config "$TMP_DIR/explicit.json" NOTICE EXPLICIT_PASS >"$TMP_DIR/prec2.out" 2>&1 )
check "--config takes precedence over autodiscovery"
contains EXPLICIT_PASS "$TMP_DIR/prec.log"

check "Missing explicit config file is rejected"
bash -c '"$1" --config "$2" INFO test >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN" "$TMP_DIR/missing.json"

printf '%s' '{bad json' >"$TMP_DIR/bad.json"
check "Malformed config file is rejected"
bash -c '"$1" --config "$2" INFO test >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN" "$TMP_DIR/bad.json"

printf '%s' '{"levels":{"X":{"value":1,"color":"notacolor"}}}' >"$TMP_DIR/badcolor.json"
check "Unknown config color is rejected"
bash -c '"$1" --config "$2" INFO test >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$LOG_BIN" "$TMP_DIR/badcolor.json"

printf '%s' '{oops' >"$TMP_DIR/disc/.logamuffin"
check "Malformed autodiscovered config is rejected"
bash -c 'cd "$1"; LOG_FILE="$2" "$3" INFO test >/dev/null 2>&1; [[ $? -eq 2 ]]' _ "$TMP_DIR/disc" "$TMP_DIR/bad-auto.log" "$LOG_BIN"

# ------------------------------------------------------------
# Summary
# ------------------------------------------------------------

printf '\nResults: %d passed, %d failed\n' "$pass" "$fail"

if ((fail > 0)); then
    exit 1
fi
