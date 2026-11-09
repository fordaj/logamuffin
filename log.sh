#!/usr/bin/env bash
# logamuffin `log` helper (issue #2).
#
# Source this file to get a `log` function that wraps the logamuffin
# executable so you can write:
#
#   log TRACE "Detailed trace"
#   log INFO "Starting application"
#   log 50 "Something failed"
#   log WARNING "Retry in 3 seconds"
#
# instead of:
#
#   logamuffin INFO "Starting application"
#
# The wrapper handles everything the `logamuffin` command needs so
# scripts do not have to set up environment variables by hand:
#
#   * LOG_SOURCE — set per call to the caller's file and line number
#     (plus the calling function name when available), so log output
#     shows where each message came from. The executable reduces
#     LOG_SOURCE to its basename, keeping the ":line" suffix intact.
#   * LOG_LEVEL / LOG_FILE / LOG_COLOR / LOG_CONFIG / LOG_LEVELS —
#     inherited from the caller, so export them once per script and
#     the helper (and `logamuffin`) respects them. When unset,
#     `logamuffin` defaults apply (INFO threshold,
#     .logs/log.log, automatic color).
#   * LEVEL — a word (TRACE, INFO, ERROR, WARN, CRITICAL, custom
#     categories, …) or a numeric value (50, 25, …), forwarded as-is
#     to `logamuffin` (issue #3).
#   * MESSAGE — every argument after LEVEL is joined with spaces by
#     `logamuffin`; quote messages with spaces as usual.
#
# Install:
#   # copy log.sh somewhere on PATH, or reference it directly:
#   source /path/to/log.sh
#   # or paste the function into ~/.bashrc (see README: "Bash helper").
#
# Configure the binary location with LOGAMUFFIN_BIN when
# `logamuffin` is not on PATH (e.g. in the demo or tests):
#
#   export LOGAMUFFIN_BIN="$PWD/build/debug/logamuffin"
#
# Limitations: Bash only (BASH_SOURCE / FUNCNAME / BASH_LINENO are
# Bash-specific). For Zsh, use the equivalent snippet in README.

# Resolve the logamuffin binary at call time (not source time) so
# tests and the demo can point at a build output via LOGAMUFFIN_BIN
# or PATH. Never fails at source time.
_logamuffin_bin() {
    if [[ -n "${LOGAMUFFIN_BIN:-}" ]]; then
        printf '%s\n' "$LOGAMUFFIN_BIN"
        return 0
    fi
    if command -v logamuffin >/dev/null 2>&1; then
        command -v logamuffin
        return 0
    fi
    # Last resort: the repo-local debug build (run_tests.sh output).
    local helper_dir
    helper_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    if [[ -x "$helper_dir/build/debug/logamuffin" ]]; then
        printf '%s\n' "$helper_dir/build/debug/logamuffin"
        return 0
    fi
    if [[ -x "$helper_dir/build/release/macos-arm64/logamuffin" ]]; then
        printf '%s\n' "$helper_dir/build/release/macos-arm64/logamuffin"
        return 0
    fi
    printf '%s\n' "logamuffin"
}

# log LEVEL [MESSAGE ...]
#   LEVEL may be a word (INFO) or a number (50). All arguments are
#   forwarded to `logamuffin`; the exit code is preserved.
log() {
    local bin
    bin="$(_logamuffin_bin)"

    # Caller context → LOG_SOURCE="file[:function]:line". BASH_SOURCE[1]
    # is the caller's file, BASH_LINENO[0] is the line that called
    # log(), and FUNCNAME[1] is the calling function (empty at top
    # level). Interactive shells have no caller file; fall back to $0.
    # Defaults keep `set -u` safe.
    local caller_file="${BASH_SOURCE[1]:-}"
    local caller_line="${BASH_LINENO[0]:-1}"
    local caller_func=""
    if ((${#FUNCNAME[@]} > 1)); then
        caller_func="${FUNCNAME[1]:-}"
    fi
    if [[ -z "$caller_file" ]]; then
        caller_file="${0:-log}"
    fi

    local src
    if [[ -n "$caller_func" && "$caller_func" != "log" && "$caller_func" != "main" ]]; then
        src="${caller_file}:${caller_func}:${caller_line}"
    else
        src="${caller_file}:${caller_line}"
    fi

    LOG_SOURCE="$src" "$bin" "$@"
    # Preserve `logamuffin`'s exit code (0 success/filtered,
    # 2 usage/config errors) even when `set -e` is active.
    return $?
}
