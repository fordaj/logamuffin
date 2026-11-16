#!/usr/bin/env bash
set -euo pipefail

# _level_to_value <name-or-number> prints the numeric severity:
# words map through the built-in table (unknown names -> INFO=20),
# numbers pass through unchanged.
_level_to_value() {
    local name="$1"
    if [[ "$name" =~ ^-?[0-9]+$ ]]; then
        printf '%s' "$name"
        return 0
    fi
    local upper
    upper="$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')"
    case "$upper" in
    TRACE) printf 5 ;;
    DEBUG) printf 10 ;;
    INFO) printf 20 ;;
    WARNING | WARN) printf 30 ;;
    ERROR) printf 40 ;;
    CRITICAL | FATAL) printf 50 ;;
    ALWAYS) printf -- -1 ;;
    *) printf 20 ;;
    esac
}

ENTRY_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$ENTRY_DIR/env.sh"

# _caller_source sets CALLER_SOURCE to "file[:function]:line" for the
# caller of log(). From inside this helper the stack is
# (this file is frame 0, log() is frame 1, the call site is frame 2;
# BASH_LINENO[1] is the line where log() was called). Sets a global
# rather than printing so no subshell frame is introduced.
_caller_source() {
    local caller_file="${BASH_SOURCE[2]:-}"
    local caller_line="${BASH_LINENO[1]:-1}"
    local caller_func=""
    if ((${#FUNCNAME[@]} > 2)); then
        caller_func="${FUNCNAME[2]:-}"
    fi
    if [[ -z "$caller_file" ]]; then
        caller_file="${0:-log}"
    fi

    if [[ -n "$caller_func" && "$caller_func" != "log" && "$caller_func" != "main" ]]; then
        CALLER_SOURCE="${caller_file}:${caller_func}:${caller_line}"
    else
        CALLER_SOURCE="${caller_file}:${caller_line}"
    fi
}

# _fallback LEVEL MESSAGE... — printf implementation used when no logamuffin
# binary is available (bootstrap / fresh clone). Same call shape and
# filtering semantics as the real thing; errors go to stderr, exit code 2
# only for a missing level argument.
_fallback() {
    if (($# == 0)); then
        printf '%s\n' "log: usage: log LEVEL MESSAGE..." >&2
        return 2
    fi
    local src="$1"
    shift
    local level_arg="${1:-}"
    shift
    local msg="$*"
    local level_value threshold_value
    level_value="$(_level_to_value "$level_arg")"
    local _thr_raw="${LOG_LEVEL:-${LOGAMUFFIN_LEVEL:-INFO}}"
    if [[ -z "$_thr_raw" ]]; then
        _thr_raw="INFO"
    fi
    threshold_value="$(_level_to_value "$_thr_raw")"
    if [[ "$level_value" -ne -1 && "$level_value" -lt "$threshold_value" ]]; then
        return 0
    fi
    if [[ "$level_value" -ge 40 ]]; then
        printf '%s: [%s] %s\n' "$src" "$level_arg" "$msg" >&2
    else
        printf '%s: [%s] %s\n' "$src" "$level_arg" "$msg"
    fi
    return 0
}

# log LEVEL [MESSAGE ...]
#   LEVEL may be a word (INFO) or a number (50). When a logamuffin
#   binary is available all arguments are forwarded to it with
#   LOG_SOURCE set from the call site; otherwise a printf fallback
#   keeps the same `log LEVEL MESSAGE` call shape working (see the
#   header for filtering/stream semantics). The exit code is
#   preserved (2 for usage errors, 0 otherwise for the fallback).
log() {
    local bin
    bin="$LOGAMUFFIN_BIN"

    # Caller context -> LOG_SOURCE="file[:function]:line".
    _caller_source

    if [[ ! -x "$bin" ]]; then
        _fallback "$CALLER_SOURCE" "$@"
        return $?
    fi

    LOG_SOURCE="$CALLER_SOURCE" "$bin" "$@"
    # Preserve `logamuffin`'s exit code (0 success/filtered,
    # 2 usage/config errors) even when `set -e` is active.
    return $?
}
