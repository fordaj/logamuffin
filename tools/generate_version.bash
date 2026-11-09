#!/usr/bin/env bash
# generate_version.bash - regenerate .version/version.hpp from git history
# using the bundled fordaj/Versioner binary.
#
# Runs tools/versioner/<tag>/<platform>/versioner --repo-dir <root>
# --format shell, validates the five components, and writes
# .version/version.hpp atomically. The generated header defines
# LOGAMUFFIN_VERSION_BRANCH/MAJOR/MINOR/REVISION/PATCH plus the
# LOGAMUFFIN_VERSION ("0.1.3.0.34" style) and LOGAMUFFIN_VERSION_TAG
# ("v0.1.3.0.34") string literals. Build scripts call this before
# compiling so --version always reflects the current HEAD; the
# .version/ directory is git-ignored and is not committed.
#
# Usage:
#   tools/generate_version.bash          # regenerate .version/version.hpp
#   tools/generate_version.bash --help   # usage
#
# Prerequisites: git (versioner shells out to it) and a bundled
# versioner binary for this host. Refresh the bundle with
# tools/versioner/update.bash (https://github.com/fordaj/Versioner).
#
# Env:
#   VERSIONER_BIN   Path to a prebuilt versioner (default: bundled
#                   tools/versioner/v*/<platform>/versioner[.exe]).
#   OUTPUT_DIR      Directory for version.hpp (default: .version).

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/generate_version.bash [--help]

Regenerate .version/version.hpp from git history using the bundled
Versioner binary (tools/versioner/v*/<platform>/versioner).

The output header defines LOGAMUFFIN_VERSION_BRANCH, _MAJOR, _MINOR,
_REVISION, _PATCH, LOGAMUFFIN_VERSION, and LOGAMUFFIN_VERSION_TAG.
Compile with -I.version so #include "version.hpp" finds it.

Flags:
  --help, -h      Show this help.

Env:
  VERSIONER_BIN   Path to a prebuilt versioner (default: bundled).
  OUTPUT_DIR      Directory for version.hpp (default: .version).
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --help | -h)
        usage
        exit 0
        ;;
    *)
        echo "generate_version.bash: unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

log() { printf 'generate_version.bash: %s\n' "$*"; }
die() {
    printf 'generate_version.bash: error: %s\n' "$*" >&2
    exit 1
}
need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

need_cmd git

# Repo root is the parent of tools/ (this script lives in tools/).
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
[[ -d "$REPO_ROOT" ]] || die "repo root not found: $REPO_ROOT"
cd "$REPO_ROOT"

OUTPUT_DIR="${OUTPUT_DIR:-$REPO_ROOT/.version}"
VERSION_HEADER="$OUTPUT_DIR/version.hpp"

# map_host_to_platform sets PLATFORM (e.g. Darwin/arm64 -> macos-arm64)
# or returns 1 for unknown values. Mirrors tools/versioner/update.bash and
# demo.sh so the bundled binary lookup stays consistent.
map_host_to_platform() {
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
    PLATFORM="$mapped_os-$mapped_arch"
}

# locate_versioner sets VERSIONER_PATH to the binary to run.
# VERSIONER_BIN env wins; otherwise the bundled tools/versioner/v*/
# <platform>/versioner[.exe] (newest tag dir when several exist).
locate_versioner() {
    if [[ -n "${VERSIONER_BIN:-}" ]]; then
        [[ -x "$VERSIONER_BIN" ]] || die "VERSIONER_BIN=$VERSIONER_BIN is not executable"
        VERSIONER_PATH="$VERSIONER_BIN"
        return 0
    fi
    if ! map_host_to_platform; then
        die "unsupported host for bundled versioner: $(uname -s)/$(uname -m) (set VERSIONER_BIN to a prebuilt versioner)"
    fi
    local bin_name="versioner"
    case "$PLATFORM" in
    windows-*) bin_name="versioner.exe" ;;
    esac
    local bundle_root="$REPO_ROOT/tools/versioner"
    [[ -d "$bundle_root" ]] || die "versioner bundle not found: $bundle_root (run tools/versioner/update.bash)"
    local candidate candidate_name last=""
    shopt -s nullglob
    for dir in "$bundle_root"/v*/; do
        candidate_name="$(basename "$dir")"
        candidate="$bundle_root/$candidate_name/$PLATFORM/$bin_name"
        if [[ -x "$candidate" ]]; then
            last="$candidate"
        fi
    done
    shopt -u nullglob
    if [[ -z "$last" ]]; then
        die "bundled versioner for $PLATFORM not found under $bundle_root (run: tools/versioner/update.bash; or set VERSIONER_BIN)"
    fi
    VERSIONER_PATH="$last"
}

locate_versioner
log "using versioner: $VERSIONER_PATH"
log "repo: $REPO_ROOT"

VERSION_OUT="$("$VERSIONER_PATH" --repo-dir "$REPO_ROOT" --format shell)" ||
    die "versioner failed for $REPO_ROOT"
printf '%s\n' "$VERSION_OUT" | sed 's/^/generate_version.bash:   /'

# parse_shell_export <name> prints the unquoted value of
# "export <name>=<value>" (last occurrence wins). Versioner quotes
# only unsafe values; generated versions are safe, but strip one
# layer of single quotes defensively.
parse_shell_export() {
    local name="$1" value=""
    while IFS= read -r line; do
        case "$line" in
        export\ "$name"=*)
            value="${line#export "$name"=}"
            ;;
        esac
    done <<<"$VERSION_OUT"
    # Trim surrounding single quotes if present (shellQuote form).
    if [[ "$value" == \'* && "$value" == *\' ]]; then
        value="${value:1:-1}"
    fi
    printf '%s' "$value"
}

VERSION_BRANCH="$(parse_shell_export VERSION_BRANCH)"
VERSION_MAJOR="$(parse_shell_export VERSION_MAJOR)"
VERSION_MINOR="$(parse_shell_export VERSION_MINOR)"
VERSION_REVISION="$(parse_shell_export VERSION_REVISION)"
VERSION_PATCH="$(parse_shell_export VERSION_PATCH)"
VERSION="$(parse_shell_export VERSION)"
TAG="$(parse_shell_export TAG)"

for pair in "VERSION_BRANCH:$VERSION_BRANCH" "VERSION_MAJOR:$VERSION_MAJOR" "VERSION_MINOR:$VERSION_MINOR" "VERSION_REVISION:$VERSION_REVISION" "VERSION_PATCH:$VERSION_PATCH"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    [[ "$value" =~ ^[0-9]+$ ]] || die "could not parse $name from versioner output (got '$value')"
done
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "computed version looks invalid: '$VERSION' (expected X.Y.Z.W.V)"
[[ -n "$TAG" ]] || die "could not parse TAG from versioner output"
[[ "$TAG" == "v$VERSION" ]] || die "computed tag '$TAG' does not match version '$VERSION'"

log "VERSION=$VERSION  TAG=$TAG"

mkdir -p -- "$OUTPUT_DIR"
TMP_HEADER="$OUTPUT_DIR/.version.hpp.tmp.$$"
trap 'rm -f -- "$TMP_HEADER"' EXIT

cat >"$TMP_HEADER" <<EOF
// Autogenerated by tools/generate_version.bash (fordaj/Versioner) - do not edit.
#pragma once

#define LOGAMUFFIN_VERSION_BRANCH $VERSION_BRANCH
#define LOGAMUFFIN_VERSION_MAJOR $VERSION_MAJOR
#define LOGAMUFFIN_VERSION_MINOR $VERSION_MINOR
#define LOGAMUFFIN_VERSION_REVISION $VERSION_REVISION
#define LOGAMUFFIN_VERSION_PATCH $VERSION_PATCH

#define LOGAMUFFIN_VERSION "$VERSION"
#define LOGAMUFFIN_VERSION_TAG "$TAG"
EOF

mv -- "$TMP_HEADER" "$VERSION_HEADER"
trap - EXIT
log "wrote $VERSION_HEADER"
