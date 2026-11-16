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

    # Caller context -> LOG_SOURCE="file[:function]:line". BASH_SOURCE[1]
    # is the caller's file, BASH_LINENO[0] is the line that called
    # log(), and FUNCNAME[1] is the calling function (empty at top
    # level). Defaults keep `set -u` safe.
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

    if [[ ! -x "$bin" ]]; then
        # No logamuffin binary: printf fallback (bootstrap / fresh clone).
        if [[ $# -eq 0 ]]; then
            printf '%s\n' "log: usage: log LEVEL MESSAGE..." >&2
            return 2
        fi
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
    fi

    LOG_SOURCE="$src" "$bin" "$@"
    # Preserve `logamuffin`'s exit code (0 success/filtered,
    # 2 usage/config errors) even when `set -e` is active.
    return $?
}
