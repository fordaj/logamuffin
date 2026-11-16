#!/usr/bin/env bash
# scripts/release.sh - logamuffin release (adapted from fordaj/Versioner's
# release.sh; same concept, Zig instead of Go).
#
# Uses the bundled Versioner binary to compute the next tag, regenerates
# .version/version.hpp, cross-compiles the release binaries with
# scripts/build.sh (Zig `zig c++` for the cross targets, native clang++
# for macOS), packs each platform into
# Logamuffin_<tag>_<platform>.tar.gz (.zip on Windows; flat archives:
# binary + README + LICENSE), tags HEAD, and publishes a GitHub
# release with those archives + SHA-256 checksums.
#
# Usage:
#   scripts/release.sh           # tag + push + GitHub release
#   scripts/release.sh --dry-run # compute + build only, touch nothing remote
#
# Prerequisites: zig, git, tar, zip, and (for the real release) gh
# authenticated via `gh auth login`. Working tree must be clean; HEAD
# should be pushed. The bundled Versioner binary lives under
# tools/versioner/ (refresh with tools/versioner/update.sh).

set -euo pipefail

DRY_RUN=0

usage() {
    cat <<'EOF'
Usage: scripts/release.sh [--dry-run]

Compute the next version with the bundled Versioner binary (dogfooding
concept from Versioner's release.sh), regenerate .version/version.hpp,
cross-compile the release binaries via scripts/build.sh (Zig) into
build/release/<platform>/logamuffin[.exe], stage each into
dist/<platform>/ with README.md + LICENSE, pack into
Logamuffin_<tag>_<platform>.tar.gz (.zip on Windows), create an
annotated tag, push it, and create a GitHub release with the archives
+ checksums.

Flags:
  --dry-run, -n   Build + compute version, but do not tag/push/release.
  --help, -h      Show this help.

Env:
  VERSIONER_BIN   Path to a prebuilt versioner used for version computation
                  (default: bundled tools/versioner/v*/<platform>/versioner).
  BUILD_MODE      Build mode for scripts/build.sh (default: release; the release
                  flow expects release outputs under build/release/).
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
    *)
        echo "release.sh: unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

log() { printf 'release.sh: %s\n' "$*"; }
die() {
    printf 'release.sh: error: %s\n' "$*" >&2
    exit 1
}
need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

for cmd in git zig tar zip; do need_cmd "$cmd"; done

if [[ "$DRY_RUN" -ne 1 ]]; then
    need_cmd gh
    if ! gh auth status >/dev/null 2>&1; then
        die "gh is not authenticated (run: gh auth login)"
    fi
fi

if ! REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    die "not inside a git repository"
fi
cd "$REPO_ROOT"

# Shared host detection + bundled-versioner resolution (tools/host_platform.sh).
# shellcheck source=tools/host_platform.sh
. "$REPO_ROOT/tools/host_platform.sh"

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
HEAD_SHORT="$(git rev-parse --short HEAD)"
log "repo: $REPO_ROOT"
log "branch: $BRANCH  HEAD: $HEAD_SHORT"
if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY RUN: no tag/push/release will happen"
fi

# --- Precondition: clean working tree ---------------------------------
if [[ -n "$(git status --porcelain)" ]]; then
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "warning: working tree is not clean (allowed in --dry-run)"
        git status --short | sed 's/^/release.sh:   /' >&2
    else
        die "working tree is not clean (commit or stash first):"$'\n'"$(git status --short)"
    fi
fi

# Best-effort fetch so remote tag-existence checks are current.
if ! git fetch origin --tags 2>/dev/null; then
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "warning: git fetch origin --tags failed; remote tag check may be stale"
    else
        die "git fetch origin --tags failed"
    fi
fi

# --- Dogfooding: compute VERSION/TAG with the bundled versioner (or VERSIONER_BIN) ---
locate_versioner "$REPO_ROOT" || exit 1
DOGFOOD_BIN="$VERSIONER_PATH"
log "using versioner: $DOGFOOD_BIN"

log "computing version with versioner (dogfooding)..."
VERSION_OUT="$("$DOGFOOD_BIN" --repo-dir "$REPO_ROOT" --format shell)"
log "versioner output:"
printf '%s\n' "$VERSION_OUT" | sed 's/^/release.sh:   /'

VERSION="$(parse_shell_export VERSION)"
TAG="$(parse_shell_export TAG)"
[[ -n "$VERSION" ]] || die "could not parse VERSION from versioner output"
[[ -n "$TAG" ]] || die "could not parse TAG from versioner output"
if ! [[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    die "computed tag looks invalid: '$TAG' (expected vX.Y.Z.W.V)"
fi
log "VERSION=$VERSION  TAG=$TAG"

# --- Tag must not already exist ---------------------------------------
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null 2>&1; then
    die "tag $TAG already exists locally (make a new feat:/fix: commit to bump the version, then re-run)"
fi
if git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1; then
    die "tag $TAG already exists on origin (make a new feat:/fix: commit to bump the version, then re-run)"
fi

# --- Regenerate .version/version.hpp -----------------------------------
# Same generator build.sh uses; build.sh re-runs it before compiling,
# so the header and the binaries always agree on VERSION.
log "regenerating .version/version.hpp..."
bash "$REPO_ROOT/tools/generate_version.sh"
[[ -f "$REPO_ROOT/.version/version.hpp" ]] || die "failed to generate .version/version.hpp"
if ! grep -Fq "LOGAMUFFIN_VERSION_TAG \"$TAG\"" "$REPO_ROOT/.version/version.hpp"; then
    die "generated .version/version.hpp does not contain $TAG (versioner mismatch?)"
fi

# --- Cross-compile release binaries (Zig) ------------------------------
log "cross-compiling release binaries (TAG=$TAG) via scripts/build.sh..."
BUILD_MODE="${BUILD_MODE:-release}" bash "$REPO_ROOT/scripts/build.sh"

shopt -s nullglob
build_dirs=(build/release/*/)
((${#build_dirs[@]} > 0)) || die "scripts/build.sh produced no build/release/<platform>/ directories"
log "built ${#build_dirs[@]} platform directories:"
for d in "${build_dirs[@]}"; do log "  $d"; done

# Stage dist/<platform>/ for packing: binary + README.md + LICENSE.
# scripts/build.sh outputs build/release/<platform>/logamuffin[.exe];
# archives keep the binary at the archive root (flat), as in
# Versioner's release.sh.
log "staging dist/..."
rm -rf -- dist
mkdir -p -- dist
for d in "${build_dirs[@]}"; do
    platform="$(basename "$d")"
    [[ -f "$d/logamuffin" || -f "$d/logamuffin.exe" ]] || die "no logamuffin binary in $d"
    mkdir -p "dist/$platform"
    for cand in "$d/logamuffin" "$d/logamuffin.exe"; do
        [[ -f "$cand" ]] && cp -p "$cand" "dist/$platform/"
    done
    for doc in README.md LICENSE; do
        [[ -f "$doc" ]] || die "missing $doc (needed for release archives)"
        cp -p "$doc" "dist/$platform/"
    done
done

# Native binary should report our version (guards -I.version wiring:
# logamuffin --version prints LOGAMUFFIN_VERSION, i.e. VERSION w/o v).
if HOST_PLATFORM="$(host_platform)"; then
    native_suffix=""
    case "$HOST_PLATFORM" in
    windows-*) native_suffix=".exe" ;;
    esac
    NATIVE_BIN="dist/$HOST_PLATFORM/logamuffin$native_suffix"
    if [[ -x "$NATIVE_BIN" ]]; then
        NATIVE_VER="$("$NATIVE_BIN" --version)"
        log "native binary reports: $NATIVE_VER"
        case "$NATIVE_VER" in
        *"$VERSION"*) ;;
        *) die "native binary version '$NATIVE_VER' does not contain $VERSION (generated header missing?)" ;;
        esac
    fi
fi

# Pack each platform into a platform-native archive (flat: binary +
# README.md + LICENSE at archive root). Windows targets get .zip,
# everything else gets .tar.gz.
log "packing platform-native archives..."
archives=()
for d in dist/*/; do
    platform="$(basename "$d")"
    bin=""
    for cand in "${d}logamuffin" "${d}logamuffin.exe"; do
        [[ -f "$cand" ]] && bin="$cand"
    done
    [[ -n "$bin" ]] || die "no logamuffin binary in $d"
    [[ -s "$bin" ]] || die "built binary is empty: $bin"
    bin_name="$(basename "$bin")"
    case "$platform" in
    windows-*)
        archive="dist/Logamuffin_${TAG}_${platform}.zip"
        rm -f "$archive"
        (cd "$d" && zip -q -j "../Logamuffin_${TAG}_${platform}.zip" "$bin_name" README.md LICENSE)
        ;;
    *)
        archive="dist/Logamuffin_${TAG}_${platform}.tar.gz"
        rm -f "$archive"
        (cd "$d" && tar -czf "../Logamuffin_${TAG}_${platform}.tar.gz" "$bin_name" README.md LICENSE)
        ;;
    esac
    [[ -s "$archive" ]] || die "failed to create $archive"
    log "  $archive:"
    case "$archive" in
    *.zip) unzip -l "$archive" | sed 's/^/release.sh:     /' ;;
    *) tar -tzf "$archive" | sed 's/^/release.sh:     /' ;;
    esac
    archives+=("$archive")
done

# Checksums of the release archives (portable shasum; sha256sum fallback).
log "generating checksums..."
if command -v shasum >/dev/null 2>&1; then
    (cd dist && shasum -a 256 Logamuffin_* >checksums.txt)
elif command -v sha256sum >/dev/null 2>&1; then
    (cd dist && sha256sum Logamuffin_* >checksums.txt)
else
    die "neither shasum nor sha256sum found (need one for checksums)"
fi
cat dist/checksums.txt | sed 's/^/release.sh:   /'

# --- Tag + GitHub release ----------------------------------------------
if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY RUN: would run:"
    log "  git tag -a $TAG -m \"logamuffin $TAG\""
    log "  git push origin $TAG"
    log "  gh release create $TAG --title \"logamuffin $TAG\" --generate-notes dist/Logamuffin_* dist/checksums.txt"
    log "dry run complete: VERSION=$VERSION TAG=$TAG"
    exit 0
fi

log "creating annotated tag $TAG..."
git tag -a "$TAG" -m "logamuffin $TAG"

log "pushing tag $TAG..."
git push origin "$TAG"

if gh release view "$TAG" >/dev/null 2>&1; then
    die "GitHub release $TAG already exists (tag was pushed; upload assets manually with: gh release upload $TAG dist/Logamuffin_* dist/checksums.txt --clobber)"
fi

log "creating GitHub release $TAG with ${#archives[@]} archives + checksums..."
gh release create "$TAG" \
    --title "logamuffin $TAG" \
    --generate-notes \
    "${archives[@]}" dist/checksums.txt

if REPO_SLUG="$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null)" && [[ -n "$REPO_SLUG" ]]; then
    log "release: https://github.com/$REPO_SLUG/releases/tag/$TAG"
else
    log "release $TAG created (see GitHub Releases)"
fi
