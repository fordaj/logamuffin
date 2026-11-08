#!/usr/bin/env bash
# Logamuffin feature showcase.
#
# Every capability claimed in README.md is exercised here so a user can
# visually spot regressions or spot new features when they run it:
#   ./demo.sh
#
# Sections follow the README order:
#   1 version/help, 2 severity levels + colors, 3 aliases + case + joining,
#   4 threshold filtering, 5 stdout vs stderr, 6 environment surface,
#   7 extensible categories, 8 JSON config, 9 precedence + autodiscovery,
#   10 persistent file logging, 11 concurrent writes, 12 shell patterns.
#
# Intentionally no `set -e`: several sections intentionally trigger
# expected failures (unknown level, bad config) to show exit codes.
# Without -e those demos do not abort the showcase.

set -uo pipefail

# ============================================================
# Locate Logamuffin
# ============================================================

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

resolve_logger() {
    if command -v logamuffin >/dev/null 2>&1; then
        command -v logamuffin
        return 0
    fi

    # Fresh native build from ./run_tests.sh (single-command path).
    if [[ -x "$SCRIPT_DIR/build/logamuffin" ]]; then
        printf '%s\n' "$SCRIPT_DIR/build/logamuffin"
        return 0
    fi

    # Platform build from ./build.sh.
    local build_dir
    case "$(uname -s):$(uname -m)" in
    Darwin:x86_64)
        build_dir="macos-x86_64"
        ;;
    Darwin:arm64)
        build_dir="macos-arm64"
        ;;
    MINGW*:x86_64 | MSYS*:x86_64 | CYGWIN*:x86_64)
        build_dir="windows-x86_64"
        ;;
    MINGW*:ARM64 | MSYS*:ARM64 | CYGWIN*:ARM64)
        build_dir="windows-arm64"
        ;;
    Linux:x86_64)
        build_dir="linux-x86_64"
        ;;
    Linux:aarch64 | Linux:arm64)
        build_dir="linux-arm64"
        ;;
    *)
        return 1
        ;;
    esac

    local candidate="$SCRIPT_DIR/$build_dir/logamuffin"

    # Windows build output uses the .exe suffix.
    case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        candidate="${candidate}.exe"
        ;;
    esac

    if [[ -x "$candidate" ]]; then
        printf '%s\n' "$candidate"
        return 0
    fi

    return 1
}

if ! LOGGER="$(resolve_logger)"; then
    echo "ERROR: Logamuffin not found on PATH or in expected build directory." >&2
    echo "Build it with ./build.sh, or ./run_tests.sh for a native build, or install it on PATH." >&2
    exit 1
fi

# ============================================================
# Isolated demo state (host LOG_* values must not leak in)
# ============================================================

# Unset the documented configuration surface so the showcase is
# deterministic on any machine. Per-category variables are unset
# explicitly for the categories the demo defines.
unset LOG_LEVEL LOG_SOURCE LOG_CONFIG LOG_LEVELS LOG_CATEGORIES \
    LOGAMUFFIN_LEVELS LOGAMUFFIN_CATEGORIES LOGAMUFFIN_CONFIG \
    LOGAMUFFIN_LEVEL LOGAMUFFIN_COLOR LOGAMUFFIN_FILE LOGAMUFFIN_SOURCE \
    LOG_LEVEL_VALUE_NOTICE LOG_LEVEL_COLOR_NOTICE \
    LOG_LEVEL_NOTICE_VALUE LOG_LEVEL_NOTICE_COLOR \
    LOG_LEVEL_VALUE_INFO LOG_LEVEL_COLOR_INFO \
    LOG_LEVEL_INFO_VALUE LOG_LEVEL_INFO_COLOR \
    LOG_LEVEL_VALUE_AUDIT LOG_LEVEL_COLOR_AUDIT \
    LOG_LEVEL_VALUE_WARNING LOG_LEVEL_COLOR_WARNING 2>/dev/null || true

TMP_BASE="${TMPDIR:-/tmp}"
TMP_BASE="${TMP_BASE%/}"
DEMO_TMP="$(mktemp -d "$TMP_BASE/logamuffin-demo.XXXXXXXX" 2>/dev/null || mktemp -d)"
cleanup() {
    rm -rf -- "$DEMO_TMP"
}
trap cleanup EXIT

# Default showcase settings: verbose, colorized, file-backed.
# Sections override these per command to show filtering and plain output.
export LOG_LEVEL=TRACE
export LOG_COLOR=1
export LOG_FILE="$DEMO_TMP/demo.log"

# ============================================================
# Helpers (readable headings so regressions are visible)
# ============================================================

section() {
    printf '\n'
    printf '  %s\n' "$1"
    printf '  %s\n' "============================================================"
    printf '\n'
}

note() {
    printf '  %s\n' "$1"
}

cmd() {
    # Echo the command about to run so the output is self-documenting.
    printf '  $ %s\n' "$*"
}

# ============================================================
# Header — standalone binary, cross-platform, version
# ============================================================

printf '\n'
printf '  Logamuffin — Feature Showcase\n'
printf '  ============================\n'
printf '  Binary:   %s\n' "$LOGGER"
printf '  Platform: %s %s\n' "$(uname -s)" "$(uname -m)"
printf '  Log file: %s\n' "$LOG_FILE"
printf '  Defaults: LOG_LEVEL=%s LOG_COLOR=%s\n' "$LOG_LEVEL" "$LOG_COLOR"
printf '\n'
printf '  Standalone executable: a single binary, no Python/Node/interpreter\n'
printf '  or framework required at runtime. Same CLI on macOS, Windows, Linux.\n'
printf '  No recompile needed to reconfigure (env vars / JSON config below).\n'
printf '  Run ./demo.sh again after any change to compare outputs visually.\n'

# ============================================================
# 1. Version and help
# ============================================================

section "1. Version and help (--version, --help)"

cmd "\"\$LOGGER\" --version"
"$LOGGER" --version

note '--- full --help output follows ---'
"$LOGGER" --help

# ============================================================
# 2. Severity levels and colors
# ============================================================

section "2. Severity levels and colors (values from README table)"

note 'TRACE (5) cyan, DEBUG (10) green, INFO (20) reset, WARNING (30) yellow,'
note 'ERROR (40) red, CRITICAL (50) purple, ALWAYS (-1) bypasses filtering.'
note 'ERROR and CRITICAL go to stderr (visible in balance below).'

cmd "TRACE \"Detailed execution trace\""
"$LOGGER" TRACE "Detailed execution trace"

cmd "DEBUG \"Diagnostic information\""
"$LOGGER" DEBUG "Diagnostic information"

cmd "INFO \"Everything is running normally\""
"$LOGGER" INFO "Everything is running normally"

cmd "WARNING \"Something deserves your attention\""
"$LOGGER" WARNING "Something deserves your attention"

cmd "ERROR \"An operation failed\"  (stderr)"
"$LOGGER" ERROR "An operation failed"

cmd "CRITICAL \"A critical failure occurred\"  (stderr)"
"$LOGGER" CRITICAL "A critical failure occurred"

cmd "ALWAYS \"This message always gets through\""
"$LOGGER" ALWAYS "This message always gets through"

# ============================================================
# 3. Aliases, case-insensitivity, message joining
# ============================================================

section "3. Aliases, case-insensitivity, and multi-word messages"

note 'WARN is an alias for WARNING; FATAL is an alias for CRITICAL.'
note 'Level names are case-insensitive; MESSAGE arguments join with spaces.'

cmd "WARN \"alias for WARNING\""
"$LOGGER" WARN "alias for WARNING"

cmd "WARNING \"alias for WARN\""
"$LOGGER" WARNING "alias for WARN"

cmd "FATAL \"alias for CRITICAL\"  (stderr)"
"$LOGGER" FATAL "alias for CRITICAL"

cmd "info \"lowercase level still works\""
"$LOGGER" info "lowercase level still works"

cmd "INFO \"hello\" \"world\" \"multiple\" \"words\""
"$LOGGER" INFO "hello" "world" "multiple" "words"

cmd "INFO \"key=value: a:b, with spaces and punctuation\""
"$LOGGER" INFO "key=value: a:b, with spaces and punctuation"

# ============================================================
# 4. Filtering by threshold (LOG_LEVEL)
# ============================================================

section "4. Filtering by LOG_LEVEL (named, numeric, ALWAYS bypass)"

note 'Default threshold is INFO: TRACE and DEBUG are suppressed.'
note 'Filtered messages still exit 0 and write nothing (no line = filtered).'

cmd "LOG_LEVEL=INFO TRACE \"filtered below INFO (no output)\""
LOG_LEVEL=INFO "$LOGGER" TRACE "filtered below INFO (no output)"

cmd "LOG_LEVEL=INFO DEBUG \"filtered below INFO (no output)\""
LOG_LEVEL=INFO "$LOGGER" DEBUG "filtered below INFO (no output)"

cmd "LOG_LEVEL=INFO INFO \"shown at INFO\""
LOG_LEVEL=INFO "$LOGGER" INFO "shown at INFO"

cmd "LOG_LEVEL=TRACE DEBUG \"verbose: DEBUG passes at TRACE\""
LOG_LEVEL=TRACE "$LOGGER" DEBUG "verbose: DEBUG passes at TRACE"

cmd "LOG_LEVEL=WARNING INFO \"filtered below WARNING (no output)\""
LOG_LEVEL=WARNING "$LOGGER" INFO "filtered below WARNING (no output)"

cmd "LOG_LEVEL=WARNING WARNING \"shown at WARNING\""
LOG_LEVEL=WARNING "$LOGGER" WARNING "shown at WARNING"

cmd "LOG_LEVEL=WARNING ERROR \"shown above WARNING\"  (stderr)"
LOG_LEVEL=WARNING "$LOGGER" ERROR "shown above WARNING"

cmd "LOG_LEVEL=CRITICAL ALWAYS \"ALWAYS bypasses CRITICAL threshold\""
LOG_LEVEL=CRITICAL "$LOGGER" ALWAYS "ALWAYS bypasses CRITICAL threshold"

cmd "LOG_LEVEL=25 INFO \"filtered: INFO=20 below numeric 25 (no output)\""
LOG_LEVEL=25 "$LOGGER" INFO "filtered: INFO=20 below numeric 25 (no output)"

cmd "LOG_LEVEL=25 WARNING \"shown: WARNING=30 at/above numeric 25\""
LOG_LEVEL=25 "$LOGGER" WARNING "shown: WARNING=30 at/above numeric 25"

# ============================================================
# 5. stdout vs stderr + redirection
# ============================================================

section "5. Output streams: stdout vs stderr, and redirection"

note 'Below 40 goes to stdout; at/above 40 (and ERROR/CRITICAL/FATAL names)'
note 'goes to stderr. Redirect the two streams independently.'

cmd "INFO (stdout)"
"$LOGGER" INFO "stdout example: INFO goes to standard output"

cmd "ERROR (stderr)"
"$LOGGER" ERROR "stderr example: ERROR goes to standard error"

note '--- redirection example (plain, LOG_COLOR=0 for readable files) ---'
: >"$DEMO_TMP/app.out"
: >"$DEMO_TMP/app.err"
cmd "INFO >> app.out; ERROR 2>> app.err"
LOG_COLOR=0 "$LOGGER" INFO "Processing files" >"$DEMO_TMP/app.out" 2>"$DEMO_TMP/app.err"
LOG_COLOR=0 "$LOGGER" ERROR "Processing failed" >>"$DEMO_TMP/app.out" 2>>"$DEMO_TMP/app.err"

printf '  --- app.out ---\n'
cat "$DEMO_TMP/app.out"
printf '  --- app.err ---\n'
cat "$DEMO_TMP/app.err"

# ============================================================
# 6. Environment surface (colors, log file, source)
# ============================================================

section "6. Environment surface: LOG_COLOR, LOG_FILE, LOG_SOURCE"

note 'LOG_COLOR=1/TRUE/YES/ON enables colors; anything else disables.'
note 'Config applies per invocation; export to configure a whole script.'

cmd "LOG_COLOR=1 INFO \"colorized message\""
LOG_COLOR=1 "$LOGGER" INFO "colorized message"

cmd "LOG_COLOR=TRUE INFO \"TRUE also enables color\""
LOG_COLOR=TRUE "$LOGGER" INFO "TRUE also enables color"

cmd "LOG_COLOR=0 INFO \"plain message (no color codes)\""
LOG_COLOR=0 "$LOGGER" INFO "plain message (no color codes)"

cmd "(unset LOG_COLOR) INFO \"auto: captured output is plain, TTY colors\""
( unset LOG_COLOR; LOG_LEVEL=TRACE "$LOGGER" INFO "auto color: captured is plain (TTY would color)" )

cmd "LOG_SOURCE=my-app.sh INFO \"custom source label\""
LOG_SOURCE="my-app.sh" "$LOGGER" INFO "custom source label"

cmd "LOG_SOURCE=/some/dir/custom.sh INFO \"source reduces to basename\""
LOG_SOURCE="/some/dir/custom.sh" "$LOGGER" INFO "source reduces to basename"

cmd "LOG_FILE=custom.log INFO \"custom log file path\""
LOG_FILE="$DEMO_TMP/custom.log" "$LOGGER" INFO "custom log file path"
printf '  --- custom log file ---\n'
cat "$DEMO_TMP/custom.log"

# ============================================================
# 7. Extensible categories via environment
# ============================================================

section "7. Extensible categories via environment (LOG_LEVELS and friends)"

note 'Each entry is NAME=VALUE:COLOR (comma/semicolon separated).'
note 'NAME=VALUE and NAME=COLOR alone also work (value kept for existing).'

cmd "LOG_LEVELS='NOTICE=25:blue,INFO=20:green' NOTICE \"custom NOTICE\""
LOG_LEVELS='NOTICE=25:blue,INFO=20:green' LOG_LEVEL=TRACE "$LOGGER" NOTICE "custom NOTICE"

cmd "LOG_LEVELS='NOTICE=25:blue;AUDIT=15:hi-yellow' AUDIT (semicolon separator)"
LOG_LEVELS='NOTICE=25:blue;AUDIT=15:hi-yellow' LOG_LEVEL=TRACE "$LOGGER" AUDIT "custom AUDIT (semicolon list)"

note '--- color formats: names, prefixes, hex truecolor, SGR, bracket ---'
note 'HEARTBEAT=3 needs LOG_LEVEL=0 because TRACE=5 would filter value 3.'
COLOR_SET='HEARTBEAT=3:#ff8800,PRIORITY=45:31,ALERT=45:[91m,QUIET=12:gray'
cmd "LOG_LEVELS='$COLOR_SET' HEARTBEAT"
LOG_LEVELS="$COLOR_SET" LOG_LEVEL=0 "$LOGGER" HEARTBEAT "hex truecolor #ff8800"

cmd "LOG_LEVELS='$COLOR_SET' PRIORITY (bare SGR 31, stderr)"
LOG_LEVELS="$COLOR_SET" LOG_LEVEL=0 "$LOGGER" PRIORITY "bare SGR code 31"

cmd "LOG_LEVELS='$COLOR_SET' ALERT (bracket SGR, stderr)"
LOG_LEVELS="$COLOR_SET" LOG_LEVEL=0 "$LOGGER" ALERT "bracket SGR [91m (stderr: 45)"

cmd "LOG_LEVELS='$COLOR_SET' QUIET (gray)"
LOG_LEVELS="$COLOR_SET" LOG_LEVEL=0 "$LOGGER" QUIET "gray (hi-black)"

note '--- prefix families: silver, bg-, underline-, bold-hi- ---'
PREFIX_SET='SILVERED=33:silver,BGED=36:bg-blue,UNDER=18:underline-cyan,BOLDHI=22:bold-hi-green'
cmd "LOG_LEVELS='$PREFIX_SET' (4 colors)"
LOG_LEVELS="$PREFIX_SET" LOG_LEVEL=TRACE "$LOGGER" SILVERED "silver (hi-white)"
LOG_LEVELS="$PREFIX_SET" LOG_LEVEL=TRACE "$LOGGER" BGED "background blue"
LOG_LEVELS="$PREFIX_SET" LOG_LEVEL=TRACE "$LOGGER" UNDER "underline cyan"
LOG_LEVELS="$PREFIX_SET" LOG_LEVEL=TRACE "$LOGGER" BOLDHI "bold hi green"

cmd "LOG_LEVELS='INFO=green' INFO \"INFO recolored, value kept\""
LOG_LEVELS='INFO=green' LOG_LEVEL=TRACE "$LOGGER" INFO "INFO recolored, value kept"

note '--- aliases and single-category overrides ---'
cmd "LOG_CATEGORIES='NOTICE=25:blue' NOTICE (LOG_LEVELS alias)"
LOG_CATEGORIES='NOTICE=25:blue' LOG_LEVEL=TRACE "$LOGGER" NOTICE "via LOG_CATEGORIES"

cmd "LOGAMUFFIN_LEVELS='NOTICE=25:blue' NOTICE (LOGAMUFFIN_* equivalent)"
LOGAMUFFIN_LEVELS='NOTICE=25:blue' LOG_LEVEL=TRACE "$LOGGER" NOTICE "via LOGAMUFFIN_LEVELS"

cmd "LOG_LEVELS='NOTICE=25:blue' LOG_CATEGORIES='NOTICE=5:red' NOTICE (LOG_LEVELS wins per entry)"
LOG_LEVELS='NOTICE=25:blue' LOG_CATEGORIES='NOTICE=5:red' LOG_LEVEL=INFO "$LOGGER" NOTICE "LEVELS wins over CATEGORIES (25 passes at INFO)"

note 'Per-category vars override existing/custom categories (define new ones'
note 'with LOG_LEVELS or JSON). They win over LOG_LEVELS in the same invocation.'

cmd "LOG_LEVEL_VALUE_INFO=10 LOG_LEVEL=WARN INFO \"filtered (INFO now 10)\""
LOG_LEVEL_VALUE_INFO=10 LOG_LEVEL=WARN "$LOGGER" INFO "filtered: INFO now 10 below WARN"

cmd "LOG_LEVEL_COLOR_INFO=green LOG_LEVEL=TRACE INFO \"INFO recolored green\""
LOG_LEVEL_COLOR_INFO=green LOG_LEVEL=TRACE "$LOGGER" INFO "INFO recolored green"

cmd "LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL_VALUE_NOTICE=5 LOG_LEVEL=INFO NOTICE \"filtered (NOTICE now 5)\""
LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL_VALUE_NOTICE=5 LOG_LEVEL=INFO "$LOGGER" NOTICE "filtered: NOTICE now 5 below INFO"

cmd "LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL_COLOR_NOTICE=green NOTICE \"green wins\""
LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL_COLOR_NOTICE=green LOG_LEVEL=TRACE "$LOGGER" NOTICE "green wins over blue"

cmd "LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL_NOTICE_VALUE=5 LOG_LEVEL=INFO NOTICE (alt spelling)"
LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL_NOTICE_VALUE=5 LOG_LEVEL=INFO "$LOGGER" NOTICE "filtered via LOG_LEVEL_NOTICE_VALUE"

note '--- custom thresholds and stderr routing ---'
cmd "LOG_LEVELS='ALERT=45:red' ALERT \"ALERT (45) goes to stderr\""
LOG_LEVELS='ALERT=45:red' LOG_LEVEL=TRACE "$LOGGER" ALERT "ALERT (45) goes to stderr"

cmd "LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL=NOTICE INFO \"filtered (INFO=20 below NOTICE=25, no output)\""
LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL=NOTICE "$LOGGER" INFO "filtered (INFO=20 below NOTICE=25, no output)"

cmd "LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL=NOTICE NOTICE \"shown at NOTICE\""
LOG_LEVELS='NOTICE=25:blue' LOG_LEVEL=NOTICE "$LOGGER" NOTICE "shown at NOTICE"

cmd "LOG_LEVELS='NOTICE=5:blue' LOG_LEVEL=NOTICE INFO \"INFO passes: redefining NOTICE moves the threshold\""
LOG_LEVELS='NOTICE=5:blue' LOG_LEVEL=NOTICE "$LOGGER" INFO "INFO passes: redefining NOTICE moves the threshold"

cmd "LOG_LEVELS='ERROR=10:red' ERROR \"ERROR forced low still goes to stderr (name wins)\""
LOG_LEVELS='ERROR=10:red' LOG_LEVEL=TRACE "$LOGGER" ERROR "ERROR forced low still goes to stderr (name wins)"

note '--- unknown color is rejected (exit 2, no output above) ---'
cmd "LOG_LEVELS='BOGUS=20:notacolor' INFO \"should fail with exit 2\""
LOG_LEVELS='BOGUS=20:notacolor' LOG_LEVEL=TRACE "$LOGGER" INFO "should fail with exit 2" 2>&1
printf '  (exit %s)\n' "$?"

# ============================================================
# 8. JSON config file
# ============================================================

section "8. JSON config file (--config, LOG_CONFIG, autodiscovery)"

note 'Object form: {level, color, file, source, levels}. Array form also works.'

if [[ -f "$DEMO_TMP/config.json" ]]; then
    rm -f "$DEMO_TMP/config.json"
fi

cat >"$DEMO_TMP/config.json" <<'JSON'
{
  "level": "TRACE",
  "color": true,
  "source": "json-app",
  "levels": {
    "TRACE": {"value": 5, "color": "cyan"},
    "INFO": {"value": 20, "color": "reset"},
    "NOTICE": {"value": 25, "color": "blue"},
    "ERROR": {"value": 40, "color": "bold-red"}
  }
}
JSON

cmd "--config config.json NOTICE (object form)"
"$LOGGER" --config "$DEMO_TMP/config.json" NOTICE "via --config object form"

cmd "--config=config.json NOTICE (= form)"
"$LOGGER" --config="$DEMO_TMP/config.json" NOTICE "via --config= form"

cmd "LOG_CONFIG=config.json NOTICE (via environment)"
LOG_CONFIG="$DEMO_TMP/config.json" "$LOGGER" NOTICE "via LOG_CONFIG"

cat >"$DEMO_TMP/array.json" <<'JSON'
{"levels": [{"name": "NOTICE", "value": 25, "color": "blue"}, {"name": "HEARTBEAT", "value": 3, "color": "#ff8800"}]}
JSON

cmd "--config array.json HEARTBEAT (array form, truecolor)"
LOG_LEVEL=0 "$LOGGER" --config "$DEMO_TMP/array.json" HEARTBEAT "via --config array form"

cat >"$DEMO_TMP/bracket.json" <<'JSON'
{"level": "TRACE", "levels": {"ALERT": {"value": 45, "color": "[0;91m"}}}
JSON

cmd "--config bracket.json ALERT (bracket SGR [0;91m, stderr: needs JSON, LOG_LEVELS splits on ';')"
LOG_COLOR=1 "$LOGGER" --config "$DEMO_TMP/bracket.json" ALERT "bracket SGR via JSON"

cat >"$DEMO_TMP/alias.json" <<'JSON'
{"threshold": "TRACE", "colors": true, "source": "alias-app", "ignored_future_key": 123, "categories": {"NOTICE": {"level": 25, "colour": "BrightRed"}, "HEARTBEAT": {"value": 3, "color": "0;91"}, "SHORTHEX": {"value": 32, "color": "#f80"}}}
JSON

note 'Top-level aliases (threshold/colors/categories) and field aliases'
note '(level/colour) work; unknown top-level keys are ignored.'
cmd "--config alias.json NOTICE (threshold/colors/categories + level/colour)"
"$LOGGER" --config "$DEMO_TMP/alias.json" NOTICE "via alias keys (BrightRed)"

cmd "--config alias.json HEARTBEAT (bare SGR 0;91 needs JSON: ';' safe here)"
LOG_LEVEL=0 "$LOGGER" --config "$DEMO_TMP/alias.json" HEARTBEAT "bare SGR 0;91 via JSON"

cmd "--config alias.json SHORTHEX (#rgb short hex)"
"$LOGGER" --config "$DEMO_TMP/alias.json" SHORTHEX "short hex #f80"

cat >"$DEMO_TMP/shorthand.json" <<'JSON'
{"level": "TRACE", "levels": ["NOTICE=25:blue", "AUDIT=15:hi-yellow"]}
JSON

cmd "--config shorthand.json NOTICE (NAME=VALUE:COLOR shorthand)"
"$LOGGER" --config "$DEMO_TMP/shorthand.json" NOTICE "via config shorthand string"

cat >"$DEMO_TMP/bare.json" <<'JSON'
{"level": "TRACE", "levels": {"AUDIT": 15, "INFO": "green"}}
JSON

cmd "--config bare.json AUDIT (bare value) + INFO (bare color)"
"$LOGGER" --config "$DEMO_TMP/bare.json" AUDIT "bare numeric value"
"$LOGGER" --config "$DEMO_TMP/bare.json" INFO "bare color string"

note '--- missing / malformed explicit config exits 2 ---'
cmd "--config missing.json INFO (expected exit 2)"
"$LOGGER" --config "$DEMO_TMP/missing.json" INFO "missing config" 2>&1
printf '  (exit %s)\n' "$?"

printf '%s' '{bad json' >"$DEMO_TMP/bad.json"
cmd "--config bad.json INFO (expected exit 2)"
"$LOGGER" --config "$DEMO_TMP/bad.json" INFO "malformed config" 2>&1
printf '  (exit %s)\n' "$?"

# ============================================================
# 9. Precedence + autodiscovery
# ============================================================

section "9. Configuration precedence and autodiscovery (./.logamuffin)"

note 'Lowest to highest: defaults, ./.logamuffin, LOG_CONFIG, --config,'
note 'then environment variables (LOG_LEVELS, LOG_LEVEL, LOG_COLOR, ...).'
note 'Only the winning file is read; --config overrides the others.'

mkdir -p "$DEMO_TMP/disc" "$DEMO_TMP/empty"
cat >"$DEMO_TMP/disc/.logamuffin" <<'JSON'
{"level": "INFO", "levels": {"NOTICE": {"value": 5, "color": "red"}}}
JSON

printf '  --- ./.logamuffin present (NOTICE=5, level INFO) ---\n'
cmd "(cd disc && LOG_FILE=disc.log LOG_LEVEL=INFO logamuffin NOTICE) -> filtered (no output)"
( cd "$DEMO_TMP/disc" && LOG_FILE="$DEMO_TMP/disc.log" LOG_LEVEL=INFO "$LOGGER" NOTICE "autodiscovered NOTICE filtered" >"$DEMO_TMP/disc1.out" 2>"$DEMO_TMP/disc1.err" )
printf '  (autodiscovered output: %s bytes; empty = filtered)\n' "$(cat "$DEMO_TMP/disc1.out" "$DEMO_TMP/disc1.err" | wc -c | tr -d ' ')"

cat >"$DEMO_TMP/explicit.json" <<'JSON'
{"levels": {"NOTICE": {"value": 25, "color": "blue"}}}
JSON

printf '  --- --config explicit.json (NOTICE=25) overrides autodiscovered 5 ---\n'
cmd "(cd disc && logamuffin --config explicit.json NOTICE)"
( cd "$DEMO_TMP/disc" && LOG_FILE="$DEMO_TMP/disc.log" LOG_LEVEL=INFO "$LOGGER" --config "$DEMO_TMP/explicit.json" NOTICE "explicit NOTICE passes (25)" )

printf '  --- LOG_CONFIG explicit.json also wins over autodiscovery ---\n'
cmd "(cd disc && LOG_CONFIG=explicit.json logamuffin NOTICE)"
( cd "$DEMO_TMP/disc" && LOG_FILE="$DEMO_TMP/disc.log" LOG_LEVEL=INFO LOG_CONFIG="$DEMO_TMP/explicit.json" "$LOGGER" NOTICE "via LOG_CONFIG over autodiscovery" )

printf '  --- environment overrides the winning file (color) ---\n'
cmd "LOG_LEVEL_COLOR_NOTICE=green --config explicit.json NOTICE (green wins over blue)"
LOG_COLOR=1 LOG_LEVEL=TRACE LOG_LEVEL_COLOR_NOTICE=green "$LOGGER" --config "$DEMO_TMP/explicit.json" NOTICE "env green overrides file blue"

cat >"$DEMO_TMP/threshold-file.json" <<'JSON'
{"level": "ERROR", "levels": {}}
JSON

printf '  --- environment overrides the file threshold ---\n'
note 'File threshold needs LOG_LEVEL unset (demo global is TRACE).'
cmd "(unset LOG_LEVEL) --config threshold-file.json INFO -> filtered (file ERROR)"
( unset LOG_LEVEL; "$LOGGER" --config "$DEMO_TMP/threshold-file.json" INFO "file ERROR filters INFO" )

cmd "LOG_LEVEL=TRACE --config threshold-file.json INFO -> shown (env TRACE wins)"
LOG_LEVEL=TRACE "$LOGGER" --config "$DEMO_TMP/threshold-file.json" INFO "env TRACE wins over file ERROR"

printf '  --- no autodiscovered file: defaults apply ---\n'
cmd "(cd empty && logamuffin INFO) (missing ./.logamuffin = defaults)"
( cd "$DEMO_TMP/empty" && LOG_FILE="$DEMO_TMP/empty.log" LOG_LEVEL=TRACE "$LOGGER" INFO "missing autodiscovery uses defaults" )

note 'Note: a malformed autodiscovered ./.logamuffin is an error (exit 2),'
note 'not a silent fallback to defaults.'

# ============================================================
# 10. Persistent file logging
# ============================================================

section "10. Persistent file logging (append, plain text, nested dirs)"

note 'Messages append across invocations; file is plain text (no ANSI).'

: >"$DEMO_TMP/persist.log"
cmd "INFO first (append 1)"
LOG_FILE="$DEMO_TMP/persist.log" "$LOGGER" INFO "first persisted message"

cmd "INFO second (append 2)"
LOG_FILE="$DEMO_TMP/persist.log" "$LOGGER" INFO "second persisted message"

printf '  --- persist.log (append order preserved) ---\n'
cat "$DEMO_TMP/persist.log"

cmd "LOG_FILE=deep/nest/log.txt INFO (creates parent dirs)"
LOG_FILE="$DEMO_TMP/deep/nest/log.txt" "$LOGGER" INFO "nested directory created"
printf '  --- deep/nest/log.txt ---\n'
cat "$DEMO_TMP/deep/nest/log.txt"

: >"$DEMO_TMP/filtered.log"
cmd "filtered message does not append"
LOG_LEVEL=INFO LOG_FILE="$DEMO_TMP/filtered.log" "$LOGGER" TRACE "filtered, nothing appended"
printf '  (filtered.log %s bytes; 0 = nothing appended)\n' "$(cat "$DEMO_TMP/filtered.log" | wc -c | tr -d ' ')"

mkdir -p "$DEMO_TMP/default"
printf '  --- default .logs/log.log (no LOG_FILE, cwd-relative) ---\n'
cmd "(cd default && unset LOG_FILE && logamuffin INFO) creates .logs/log.log"
( cd "$DEMO_TMP/default" && unset LOG_FILE && LOG_LEVEL=TRACE LOG_COLOR=0 "$LOGGER" INFO "default file path" )
printf '  --- default/.logs/log.log ---\n'
cat "$DEMO_TMP/default/.logs/log.log"

# ============================================================
# 11. Concurrent-write protection
# ============================================================

section "11. Concurrent-write protection (locked append)"

note '20 parallel writers must all appear, one line each, no corruption.'
note 'Terminal output suppressed; the file below is the verification.'

: >"$DEMO_TMP/concurrent.log"
for ((i = 1; i <= 20; i++)); do
    LOG_COLOR=0 LOG_LEVEL=TRACE LOG_FILE="$DEMO_TMP/concurrent.log" "$LOGGER" INFO "concurrent-$i" >/dev/null &
done
wait

total="$(awk 'END {print NR + 0}' "$DEMO_TMP/concurrent.log")"
printf '  %s lines in concurrent.log (expected 20)\n' "$total"
cat "$DEMO_TMP/concurrent.log"

# ============================================================
# 12. Shell-friendly patterns and exit codes
# ============================================================

section "12. Shell-friendly patterns and exit codes"

note 'Used naturally in scripts, pipelines, and automation.'

cmd "if ! false; then logamuffin ERROR \"simulated failure\"; fi"
if ! false; then
    "$LOGGER" ERROR "simulated failure (shell pattern)"
fi

cmd "logamuffin INFO \"Building application\" (build-script pattern)"
"$LOGGER" INFO "Building application"
"$LOGGER" INFO "Build completed successfully"

cmd "export LOG_LEVEL=DEBUG; DEBUG \"whole-script configuration\""
export LOG_LEVEL=DEBUG
"$LOGGER" DEBUG "whole-script DEBUG configuration"
export LOG_LEVEL=TRACE

cmd "INFO \"piped message\" | tr 'a-z' 'A-Z' (pipeline)"
LOG_COLOR=0 "$LOGGER" INFO "piped message" | tr 'a-z' 'A-Z'

note '--- exit codes ---'
"$LOGGER" INFO "success exits 0"
printf '  (exit %s)\n' "$?"

LOG_LEVEL=WARNING "$LOGGER" INFO "filtered exits 0"
printf '  (exit %s)\n' "$?"

"$LOGGER" BOGUS "unknown level" 2>"$DEMO_TMP/err"
printf '  unknown level exit %s (expected 2)\n' "$?"
cat "$DEMO_TMP/err"

"$LOGGER" 2>"$DEMO_TMP/err"
printf '  missing arguments exit %s (expected 2)\n' "$?"
cat "$DEMO_TMP/err"

# ============================================================
# Footer
# ============================================================

printf '\n'
printf '  Demo complete.\n'
printf '  Terminal lines are colorized; file lines are plain text.\n'
printf '  ERROR and CRITICAL (and any category at 40+) go to stderr.\n'
printf '  Temporary demo files under %s will be removed on exit.\n' "$DEMO_TMP"
printf '\n'
