#!/usr/bin/env bash
# update.bash — refresh bundled versioner binaries from fordaj/Versioner releases.
#
# Downloads the latest release's per-platform executables via gh, verifies
# SHA256 checksums, extracts each binary into tools/versioner/<tag>/<platform>/
# (platform matches get_platform_dir(): macos/linux/windows + x86_64/arm64,
# so darwin-amd64 -> macos-x86_64, etc.), and deletes old version folders.
# Only the binary is kept; docs (README.md, LICENSE, *.example.json) are
# discarded. Old v*/ folders are deleted only after the new version is
# fully staged, so a failed download leaves the previous bundle intact.
#
# Usage:
#   tools/versioner/update.bash              # download latest, replace old versions
#   tools/versioner/update.bash --dry-run    # resolve tag + list assets, change nothing
#   tools/versioner/update.bash --help       # usage
#
# Prerequisites: gh (https://cli.github.com/), 7z/7zz (p7zip), and (for the
# real update) shasum or sha256sum for checksum verification. The script
# resolves its own directory, so it can be run from anywhere.
#
# Layout after a successful run (example for v0.1.2.3):
#   tools/versioner/
#     update.bash
#     v0.1.2.3/
#       macos-arm64/versioner
#       macos-x86_64/versioner
#       linux-arm64/versioner
#       linux-x86_64/versioner
#       windows-x86_64/versioner.exe
#
# Env:
#   VERSIONER_REPO  Repository slug (default: fordaj/Versioner)
#   VERSIONER_TAG   Pinned tag (default: latest release)

set -euo pipefail

DRY_RUN=0
TAG="${VERSIONER_TAG:-}"
REPO="${VERSIONER_REPO:-fordaj/Versioner}"

usage() {
    cat <<'EOF'
Usage: tools/versioner/update.bash [--dry-run] [--tag TAG] [--repo REPO]

Download the latest Versioner release from GitHub via gh, extract each
platform binary into tools/versioner/<tag>/<platform>/ (platform matches
get_platform_dir(), e.g. darwin-amd64 -> macos-x86_64), and delete old
version folders. Only the binary is kept.

Flags:
  --dry-run, -n   Resolve tag + list planned assets, change nothing.
  --tag TAG       Pin a specific release tag (default: latest release).
  --repo REPO     Repository slug (default: fordaj/Versioner).
  --help, -h      Show this help.

Env:
  VERSIONER_REPO  Repository slug (overridden by --repo).
  VERSIONER_TAG   Pinned tag (overridden by --tag).
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --dry-run | -n)
        DRY_RUN=1
        shift
        ;;
    --help | -h)
        usage
        exit 0
        ;;
    --tag)
        [[ $# -ge 2 ]] || {
            echo "update.bash: --tag requires a value" >&2
            usage >&2
            exit 2
        }
        TAG="$2"
        shift 2
        ;;
    --tag=*)
        TAG="${1#--tag=}"
        shift
        ;;
    --repo)
        [[ $# -ge 2 ]] || {
            echo "update.bash: --repo requires a value" >&2
            usage >&2
            exit 2
        }
        REPO="$2"
        shift 2
        ;;
    --repo=*)
        REPO="${1#--repo=}"
        shift
        ;;
    *)
        echo "update.bash: unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

[[ -n "$REPO" ]] || {
    echo "update.bash: repository must not be empty" >&2
    exit 2
}

log() { printf 'update.bash: %s\n' "$*"; }
die() {
    printf 'update.bash: error: %s\n' "$*" >&2
    exit 1
}
need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1 (https://cli.github.com/)"
}

need_cmd gh

if [[ "$DRY_RUN" -ne 1 ]]; then
    if command -v 7zz >/dev/null 2>&1; then
        SEVENZIP=7zz
    elif command -v 7z >/dev/null 2>&1; then
        SEVENZIP=7z
    else
        die "required command not found: 7z/7zz (brew install p7zip / apt-get install p7zip-full)"
    fi
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
VERSIONER_ROOT="$SCRIPT_DIR"
[[ -d "$VERSIONER_ROOT" ]] || die "versioner bundle directory not found: $VERSIONER_ROOT"
log "versioner bundle directory: $VERSIONER_ROOT"
log "repository: $REPO"

if [[ -z "$TAG" ]]; then
    TAG="$(gh release view --repo "$REPO" --json tagName -q .tagName)" ||
        die "could not determine latest release from $REPO (is gh authenticated and the repo visible?)"
fi
[[ -n "$TAG" ]] || die "could not determine release tag from $REPO"
if ! [[ "$TAG" =~ ^v[0-9]+(\.[0-9]+)+$ ]]; then
    die "invalid tag '$TAG' (expected vX.Y.Z...)"
fi
log "release tag: $TAG"

# map_upstream_to_platform <os-arch> sets PLATFORM (e.g. darwin-arm64 -> macos-arm64)
# or returns 1 for unknown values. Sets globals to avoid subshell-die pitfalls.
map_upstream_to_platform() {
    local os_arch="$1"
    local upstream_os="${os_arch%-*}"
    local upstream_arch="${os_arch##*-}"
    local mapped_os=""
    local mapped_arch=""
    case "$upstream_os" in
    darwin) mapped_os="macos" ;;
    linux) mapped_os="linux" ;;
    windows) mapped_os="windows" ;;
    *) return 1 ;;
    esac
    case "$upstream_arch" in
    amd64 | x86_64) mapped_arch="x86_64" ;;
    arm64 | aarch64) mapped_arch="arm64" ;;
    *) return 1 ;;
    esac
    PLATFORM="$mapped_os-$mapped_arch"
}

if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY RUN: no download, extraction, or deletion will happen"
    log "release $TAG assets:"
    assets_list=$(gh release view "$TAG" --repo "$REPO" --json assets -q '.assets[].name' ||
        die "could not list assets for $TAG in $REPO")
    [[ -n "$assets_list" ]] || die "no assets found for $TAG in $REPO"
    asset_count=0
    while IFS= read -r asset; do
        [[ -n "$asset" ]] || continue
        case "$asset" in
        *.7z)
            asset_count=$((asset_count + 1))
            stem="${asset%.7z}"
            os_arch="${stem##*_}"
            if map_upstream_to_platform "$os_arch"; then
                log "  $asset -> $TAG/$PLATFORM"
            else
                log "  $asset -> unknown platform (would fail)"
            fi
            ;;
        checksums.txt)
            log "  $asset (checksums)"
            ;;
        *)
            log "  $asset (ignored)"
            ;;
        esac
    done <<<"$assets_list"
    ((asset_count > 0)) || die "no .7z assets found for $TAG in $REPO"
    shopt -s nullglob
    old_found=0
    for dir in "$VERSIONER_ROOT"/v*/; do
        [[ -d "$dir" ]] || continue
        dir_name="$(basename "$dir")"
        if [[ "$dir_name" != "$TAG" ]]; then
            log "would remove old $dir_name"
            old_found=1
        fi
    done
    if [[ "$old_found" -eq 0 ]]; then
        log "no old version folders to remove"
    fi
    log "dry run complete: would update $VERSIONER_ROOT/$TAG"
    exit 0
fi

# --- Real update: download to a temp dir, stage the bundle, then swap. ---
TMP_DIR=""
STAGE_DIR=""
cleanup() {
    if [[ -n "${TMP_DIR:-}" && -d "$TMP_DIR" ]]; then
        rm -rf -- "$TMP_DIR"
    fi
    if [[ -n "${STAGE_DIR:-}" && -d "$STAGE_DIR" ]]; then
        rm -rf -- "$STAGE_DIR"
    fi
}
trap cleanup EXIT

TMP_DIR="$(mktemp -d)"
STAGE_DIR="$(mktemp -d "$VERSIONER_ROOT/.tmp.XXXXXX")"

log "downloading $TAG assets from $REPO..."
gh release download "$TAG" --repo "$REPO" --dir "$TMP_DIR" --pattern '*.7z' --pattern 'checksums.txt' --clobber

shopt -s nullglob
archives=("$TMP_DIR"/*.7z)
((${#archives[@]} > 0)) || die "no .7z assets downloaded from $REPO $TAG"
log "downloaded ${#archives[@]} .7z archives"

if [[ -f "$TMP_DIR/checksums.txt" ]]; then
    log "verifying checksums..."
    if command -v shasum >/dev/null 2>&1; then
        (cd "$TMP_DIR" && shasum -a 256 -c checksums.txt) || die "checksum verification failed for $TAG"
    elif command -v sha256sum >/dev/null 2>&1; then
        (cd "$TMP_DIR" && sha256sum -c checksums.txt) || die "checksum verification failed for $TAG"
    else
        die "checksums.txt present but neither shasum nor sha256sum found"
    fi
    log "checksums verified"
else
    log "warning: no checksums.txt in $TAG release; skipping verification"
fi

for archive in "${archives[@]}"; do
    base="$(basename "$archive")"
    stem="${base%.7z}"
    os_arch="${stem##*_}"
    if ! map_upstream_to_platform "$os_arch"; then
        die "unknown platform in asset '$base' (expected <os>-<arch> such as darwin-arm64)"
    fi
    if [[ -e "$STAGE_DIR/$PLATFORM" ]]; then
        die "duplicate asset for platform $PLATFORM (from $base)"
    fi
    log "extracting $base -> $TAG/$PLATFORM"
    extract_dir="$TMP_DIR/extract-$PLATFORM"
    mkdir -p "$extract_dir"
    "$SEVENZIP" x -y "$archive" -o"$extract_dir" >/dev/null
    bin_src=""
    for cand in "$extract_dir/versioner" "$extract_dir/versioner.exe"; do
        if [[ -f "$cand" ]]; then
            bin_src="$cand"
        fi
    done
    [[ -n "$bin_src" ]] || die "no versioner binary in $base (expected versioner or versioner.exe at archive root)"
    bin_name="$(basename "$bin_src")"
    target_dir="$STAGE_DIR/$PLATFORM"
    mkdir -p "$target_dir"
    cp -p "$bin_src" "$target_dir/$bin_name"
    chmod +x "$target_dir/$bin_name"
    [[ -s "$target_dir/$bin_name" ]] || die "installed binary is empty: $target_dir/$bin_name"
    log "installed $PLATFORM/$bin_name (from $base)"
    rm -rf -- "$extract_dir"
done

TAG_DIR="$VERSIONER_ROOT/$TAG"
rm -rf -- "$TAG_DIR"
mkdir -p "$VERSIONER_ROOT"
mv "$STAGE_DIR" "$TAG_DIR"
STAGE_DIR=""

for dir in "$VERSIONER_ROOT"/v*/; do
    [[ -d "$dir" ]] || continue
    dir_name="$(basename "$dir")"
    if [[ "$dir_name" != "$TAG" ]]; then
        log "removing old $dir_name"
        rm -rf -- "$dir"
    fi
done

# Best-effort host verification: run the bundled binary for this machine.
host_os_raw="$(uname -s | tr '[:upper:]' '[:lower:]')"
host_arch_raw="$(uname -m | tr '[:upper:]' '[:lower:]')"
case "$host_os_raw" in
darwin*) host_os="macos" ;;
linux*) host_os="linux" ;;
msys* | mingw* | cygwin* | windows* | win*) host_os="windows" ;;
*) host_os="$host_os_raw" ;;
esac
case "$host_arch_raw" in
x86_64 | amd64) host_arch="x86_64" ;;
arm64 | aarch64) host_arch="arm64" ;;
*) host_arch="$host_arch_raw" ;;
esac
host_platform="$host_os-$host_arch"
host_bin=""
for cand in "$TAG_DIR/$host_platform/versioner" "$TAG_DIR/$host_platform/versioner.exe"; do
    if [[ -x "$cand" ]]; then
        host_bin="$cand"
    fi
done
if [[ -n "$host_bin" ]]; then
    host_ver="$("$host_bin" --version 2>&1 || true)"
    log "host binary $host_platform reports: $host_ver"
    case "$host_ver" in
    *"$TAG"*) ;;
    *) log "warning: host binary version '$host_ver' does not contain $TAG" ;;
    esac
else
    log "warning: host platform $host_platform not in bundle; skipped version check"
fi

log "done: $TAG_DIR now contains:"
for dir in "$TAG_DIR"/*/; do
    [[ -d "$dir" ]] || continue
    log "  $(basename "$dir")"
done
