#!/usr/bin/env bash
# release.bash - logamuffin release (adapted from fordaj/Versioner's
# release.bash; same concept, Zig instead of Go).
#
# Uses the bundled Versioner binary to compute the next tag, regenerates
# .version/version.hpp, cross-compiles the release binaries with
# ./build.sh (Zig `zig c++` for the cross targets, native clang++ for
# macOS), packs each platform into Logamuffin_<tag>_<platform>.7z
# (binary + README + LICENSE), tags HEAD, and publishes a GitHub
# release with the .7z archives + SHA-256 checksums.
#
# Usage:
#   ./release.bash           # tag + push + GitHub release
#   ./release.bash --dry-run # compute + build only, touch nothing remote
#
# Prerequisites: zig, git, 7z (brew install p7zip / apt install
# p7zip-full), and (for the real release) gh authenticated via
# `gh auth login`. Working tree must be clean; HEAD should be pushed.
# The bundled Versioner binary lives under tools/versioner/
# (refresh with tools/versioner/update.bash).

set -euo pipefail

DRY_RUN=0

usage() {
    cat <<'EOF'
Usage: ./release.bash [--dry-run]

Compute the next version with the bundled Versioner binary (dogfooding
concept from Versioner's release.bash), regenerate .version/version.hpp,
cross-compile the release binaries via ./build.sh (Zig) into
build/release/<platform>/logamuffin[.exe], stage each into
dist/<platform>/ with README.md + LICENSE, pack into
Logamuffin_<tag>_<platform>.7z, create an annotated tag, push it, and
create a GitHub release with the .7z archives + checksums.

Flags:
  --dry-run, -n   Build + compute version, but do not tag/push/release.
  --help, -h      Show this help.

Env:
  VERSIONER_BIN   Path to a prebuilt versioner used for version computation
                  (default: bundled tools/versioner/v*/<platform>/versioner).
  BUILD_MODE      Build mode for ./build.sh (default: release; the release
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
        echo "release.bash: unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

log() { printf 'release.bash: %s\n' "$*"; }
die() {
    printf 'release.bash: error: %s\n' "$*" >&2
    exit 1
}
need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

for cmd in git zig; do need_cmd "$cmd"; done

if command -v 7zz >/dev/null 2>&1; then
    SEVENZIP=7zz
elif command -v 7z >/dev/null 2>&1; then
    SEVENZIP=7z
else
    die "required command not found: 7z/7zz (brew install p7zip / apt-get install p7zip-full)"
fi

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

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
HEAD_SHORT="$(git rev-parse --short HEAD)"
log "repo: $REPO_ROOT"
log "branch: $BRANCH  HEAD: $HEAD_SHORT"
if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY RUN: no tag/push/release will happen"
fi

# map_host_to_platform sets HOST_PLATFORM (e.g. Darwin/arm64 -> macos-arm64)
# matching build.sh output dirs. Used for the native --version check.
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
    HOST_PLATFORM="$mapped_os-$mapped_arch"
}

# --- Precondition: clean working tree ---------------------------------
if [[ -n "$(git status --porcelain)" ]]; then
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log "warning: working tree is not clean (allowed in --dry-run)"
        git status --short | sed 's/^/release.bash:   /' >&2
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

# --- Dogfooding: use bundled versioner + compute VERSION/TAG ---------
# Versioner's release builds Versioner from Go source and runs it;
# logamuffin instead uses the vendored binary under tools/versioner/
# (the same binary tools/generate_version.bash uses), since there is
# no Go toolchain in this repo. VERSIONER_BIN overrides as before.
if [[ -n "${VERSIONER_BIN:-}" ]]; then
    [[ -x "$VERSIONER_BIN" ]] || die "VERSIONER_BIN=$VERSIONER_BIN is not executable"
    DOGFOOD_BIN="$VERSIONER_BIN"
    log "using prebuilt versioner: $DOGFOOD_BIN"
else
    # Resolve the bundled binary for this host (mirrors
    # tools/generate_version.bash; newest tag dir when several exist).
    if ! map_host_to_platform; then
        die "unsupported host for bundled versioner: $(uname -s)/$(uname -m) (set VERSIONER_BIN to a prebuilt versioner)"
    fi
    bin_name="versioner"
    case "$HOST_PLATFORM" in
    windows-*) bin_name="versioner.exe" ;;
    esac
    bundle_root="$REPO_ROOT/tools/versioner"
    [[ -d "$bundle_root" ]] || die "versioner bundle not found: $bundle_root (run tools/versioner/update.bash)"
    DOGFOOD_BIN=""
    shopt -s nullglob
    for dir in "$bundle_root"/v*/; do
        candidate_name="$(basename "$dir")"
        candidate="$bundle_root/$candidate_name/$HOST_PLATFORM/$bin_name"
        if [[ -x "$candidate" ]]; then
            DOGFOOD_BIN="$candidate"
        fi
    done
    shopt -u nullglob
    [[ -n "$DOGFOOD_BIN" ]] || die "bundled versioner for $HOST_PLATFORM not found under $bundle_root (run: tools/versioner/update.bash; or set VERSIONER_BIN)"
    log "using bundled versioner: $DOGFOOD_BIN"
fi

log "computing version with versioner (dogfooding)..."
VERSION_OUT="$("$DOGFOOD_BIN" --repo-dir "$REPO_ROOT" --format shell)"
log "versioner output:"
printf '%s\n' "$VERSION_OUT" | sed 's/^/release.bash:   /'

VERSION="$(printf '%s\n' "$VERSION_OUT" | sed -n 's/^export VERSION=//p' | tail -1)"
TAG="$(printf '%s\n' "$VERSION_OUT" | sed -n 's/^export TAG=//p' | tail -1)"
# Shell format quotes unsafe values; versions are safe, strip one
# layer of single quotes defensively.
if [[ "$VERSION" == \'* && "$VERSION" == *\' ]]; then VERSION="${VERSION:1:-1}"; fi
if [[ "$TAG" == \'* && "$TAG" == *\' ]]; then TAG="${TAG:1:-1}"; fi
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
bash "$REPO_ROOT/tools/generate_version.bash"
[[ -f "$REPO_ROOT/.version/version.hpp" ]] || die "failed to generate .version/version.hpp"
if ! grep -Fq "LOGAMUFFIN_VERSION_TAG \"$TAG\"" "$REPO_ROOT/.version/version.hpp"; then
    die "generated .version/version.hpp does not contain $TAG (versioner mismatch?)"
fi

# --- Cross-compile release binaries (Zig) ------------------------------
log "cross-compiling release binaries (TAG=$TAG) via ./build.sh..."
BUILD_MODE="${BUILD_MODE:-release}" bash "$REPO_ROOT/build.sh"

shopt -s nullglob
build_dirs=(build/release/*/)
((${#build_dirs[@]} > 0)) || die "./build.sh produced no build/release/<platform>/ directories"
log "built ${#build_dirs[@]} platform directories:"
for d in "${build_dirs[@]}"; do log "  $d"; done

# Stage dist/<platform>/ for packing: binary + README.md + LICENSE.
# ./build.sh outputs build/release/<platform>/logamuffin[.exe];
# archives keep the binary at the archive root (flat), as in
# Versioner's release.bash.
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
        [[ -f "$doc" ]] || die "missing $doc (needed for .7z archives)"
        cp -p "$doc" "dist/$platform/"
    done
done

# Native binary should report our version (guards -I.version wiring:
# logamuffin --version prints LOGAMUFFIN_VERSION, i.e. VERSION w/o v).
if map_host_to_platform; then
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

# Pack each platform into Logamuffin_<tag>_<platform>.7z (flat: binary +
# README.md + LICENSE at archive root).
log "packing .7z archives with $SEVENZIP..."
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
    archive="dist/Logamuffin_${TAG}_${platform}.7z"
    rm -f "$archive"
    (cd "$d" && "$SEVENZIP" a -t7z "../Logamuffin_${TAG}_${platform}.7z" "$bin_name" README.md LICENSE >/dev/null)
    [[ -s "$archive" ]] || die "failed to create $archive"
    log "  $archive:"
    "$SEVENZIP" l "$archive" | sed 's/^/release.bash:     /'
    archives+=("$archive")
done

# Checksums of the .7z archives (portable shasum; sha256sum fallback).
log "generating checksums..."
if command -v shasum >/dev/null 2>&1; then
    (cd dist && shasum -a 256 Logamuffin_*.7z >checksums.txt)
elif command -v sha256sum >/dev/null 2>&1; then
    (cd dist && sha256sum Logamuffin_*.7z >checksums.txt)
else
    die "neither shasum nor sha256sum found (need one for checksums)"
fi
cat dist/checksums.txt | sed 's/^/release.bash:   /'

# --- Tag + GitHub release ----------------------------------------------
if [[ "$DRY_RUN" -eq 1 ]]; then
    log "DRY RUN: would run:"
    log "  git tag -a $TAG -m \"logamuffin $TAG\""
    log "  git push origin $TAG"
    log "  gh release create $TAG --title \"logamuffin $TAG\" --generate-notes dist/Logamuffin_*.7z dist/checksums.txt"
    log "dry run complete: VERSION=$VERSION TAG=$TAG"
    exit 0
fi

log "creating annotated tag $TAG..."
git tag -a "$TAG" -m "logamuffin $TAG"

log "pushing tag $TAG..."
git push origin "$TAG"

if gh release view "$TAG" >/dev/null 2>&1; then
    die "GitHub release $TAG already exists (tag was pushed; upload assets manually with: gh release upload $TAG dist/Logamuffin_*.7z dist/checksums.txt --clobber)"
fi

log "creating GitHub release $TAG with ${#archives[@]} .7z archives + checksums..."
gh release create "$TAG" \
    --title "logamuffin $TAG" \
    --generate-notes \
    "${archives[@]}" dist/checksums.txt

if REPO_SLUG="$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null)" && [[ -n "$REPO_SLUG" ]]; then
    log "release: https://github.com/$REPO_SLUG/releases/tag/$TAG"
else
    log "release $TAG created (see GitHub Releases)"
fi
