#!/usr/bin/env bash
# host_platform.sh — shared host detection + bundled-versioner resolution.
#
# Sourced (not executed) by the Bash tooling that needs to know which
# bundle platform it is running on or where the bundled Versioner binary
# lives: tools/generate_version.sh, scripts/release.sh, scripts/demo.sh.
# Keeping them here means one mapping to update when a platform is added.
# No output and no `set` options at source time; functions only.
#
# Functions:
#   host_platform                  Print e.g. macos-arm64 (Darwin/arm64).
#                                  Returns 1 for an unsupported host.
#   locate_versioner <repo_root>   Set VERSIONER_PATH to the bundled
#                                  tools/versioner/v*/<platform>/versioner[.exe]
#                                  (honors VERSIONER_BIN; newest tag dir wins
#                                  when several exist). Returns 1 and prints
#                                  a diagnostic on stderr when not found.
#   parse_shell_export <name>      Print the value of `export <name>=<value>`
#                                  from $VERSION_OUT (last occurrence wins;
#                                  one layer of surrounding single quotes is
#                                  stripped defensively).

host_platform() {
    local os arch mapped_os mapped_arch
    os="$(uname -s)"
    arch="$(uname -m)"
    case "$os" in
    Darwin) mapped_os="macos" ;;
    Linux) mapped_os="linux" ;;
    MINGW* | MSYS* | CYGWIN*) mapped_os="windows" ;;
    *) return 1 ;;
    esac
    case "$arch" in
    x86_64 | amd64) mapped_arch="x86_64" ;;
    arm64 | aarch64) mapped_arch="arm64" ;;
    *) return 1 ;;
    esac
    printf '%s-%s' "$mapped_os" "$mapped_arch"
}

locate_versioner() {
    local repo_root="$1"
    if [[ -n "${VERSIONER_BIN:-}" ]]; then
        if [[ ! -x "$VERSIONER_BIN" ]]; then
            printf 'host_platform.sh: VERSIONER_BIN=%s is not executable\n' "$VERSIONER_BIN" >&2
            return 1
        fi
        VERSIONER_PATH="$VERSIONER_BIN"
        return 0
    fi
    local platform
    if ! platform="$(host_platform)"; then
        printf 'host_platform.sh: unsupported host for bundled versioner: %s/%s (set VERSIONER_BIN to a prebuilt versioner)\n' "$(uname -s)" "$(uname -m)" >&2
        return 1
    fi
    local bin_name="versioner"
    case "$platform" in
    windows-*) bin_name="versioner.exe" ;;
    esac
    local bundle_root="$repo_root/tools/versioner"
    if [[ ! -d "$bundle_root" ]]; then
        printf 'host_platform.sh: versioner bundle not found: %s (run tools/versioner/update.sh)\n' "$bundle_root" >&2
        return 1
    fi
    local candidate candidate_name last=""
    shopt -s nullglob
    for dir in "$bundle_root"/v*/; do
        candidate_name="$(basename "$dir")"
        candidate="$bundle_root/$candidate_name/$platform/$bin_name"
        if [[ -x "$candidate" ]]; then
            last="$candidate"
        fi
    done
    shopt -u nullglob
    if [[ -z "$last" ]]; then
        printf 'host_platform.sh: bundled versioner for %s not found under %s (run: tools/versioner/update.sh; or set VERSIONER_BIN)\n' "$platform" "$bundle_root" >&2
        return 1
    fi
    VERSIONER_PATH="$last"
}

parse_shell_export() {
    local name="$1" value=""
    while IFS= read -r line; do
        case "$line" in
        export\ "$name"=*)
            value="${line#export "$name"=}"
            ;;
        esac
    done <<<"${VERSION_OUT:-}"
    # Trim surrounding single quotes if present (shellQuote form).
    if [[ "$value" == \'* && "$value" == *\' ]]; then
        value="${value:1:-1}"
    fi
    printf '%s' "$value"
}
