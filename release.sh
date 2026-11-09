#!/usr/bin/env bash
set -euo pipefail

(
    # ============================================================
    # Configuration
    # ============================================================

    # Build a fresh release before packaging.
    RUN_BUILD="${RUN_BUILD:-true}"

    # Output directory
    DIST_DIR="${DIST_DIR:-dist}"
    rm -rf $DIST_DIR

    # ============================================================
    # Setup
    # ============================================================

    SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    cd "$SCRIPT_DIR"

    BUILD_SCRIPT="$SCRIPT_DIR/build.sh"
    VERSION_HEADER="$SCRIPT_DIR/version.hpp"
    DIST_PATH="$SCRIPT_DIR/$DIST_DIR"

    # Extract version from version.hpp.
    if [[ -z "${VERSION:-}" ]]; then
        if [[ ! -f "$VERSION_HEADER" ]]; then
            echo "ERROR: Missing version.hpp" >&2
            exit 1
        fi

        version_component() {
            grep -E "^#define $1 [0-9]+$" "$VERSION_HEADER" | awk '{print $3}'
        }

        VERSION_MAJOR="$(version_component LOGAMUFFIN_VERSION_MAJOR)"
        VERSION_MINOR="$(version_component LOGAMUFFIN_VERSION_MINOR)"
        VERSION_PATCH="$(version_component LOGAMUFFIN_VERSION_PATCH)"

        if [[ -z "$VERSION_MAJOR" || -z "$VERSION_MINOR" || -z "$VERSION_PATCH" ]]; then
            echo "ERROR: Could not extract version components from $VERSION_HEADER" >&2
            echo "Expected numeric LOGAMUFFIN_VERSION_MAJOR, LOGAMUFFIN_VERSION_MINOR, and LOGAMUFFIN_VERSION_PATCH definitions." >&2
            exit 1
        fi

        VERSION="$VERSION_MAJOR.$VERSION_MINOR.$VERSION_PATCH"
    fi

    echo "Version: $VERSION"

    # Validate version for use in filenames and release tags.
    if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.-]+)?$ ]]; then
        echo "ERROR: Invalid version: $VERSION" >&2
        exit 2
    fi

    # ============================================================
    # Build
    # ============================================================

    echo "============================================================"
    echo " Logamuffin Release"
    echo "============================================================"
    echo "Version: $VERSION"
    echo "Output:  $DIST_PATH"
    echo "Build:   $RUN_BUILD"
    echo "============================================================"
    echo

    if [[ "$RUN_BUILD" == "true" ]]; then
        if [[ ! -f "$BUILD_SCRIPT" ]]; then
            echo "ERROR: Missing build.sh" >&2
            exit 1
        fi

        echo "Building release binaries..."
        BUILD_MODE=release bash "$BUILD_SCRIPT"
        echo
    elif [[ "$RUN_BUILD" != "false" ]]; then
        echo "ERROR: RUN_BUILD must be true or false" >&2
        exit 2
    fi

    # ============================================================
    # Validate tools and build outputs
    # ============================================================

    for tool in tar zip; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "ERROR: Required tool not found: $tool" >&2
            exit 1
        fi
    done

    if command -v shasum >/dev/null 2>&1; then
        HASH_COMMAND=shasum
    elif command -v sha256sum >/dev/null 2>&1; then
        HASH_COMMAND=sha256sum
    else
        echo "ERROR: Need shasum or sha256sum to generate checksums" >&2
        exit 1
    fi

    # Each entry: archive name | binary path | archive format.
    # Binaries are produced by build.sh under build/release/<platform>/
    # (issue #1); package exactly what build.sh produced.
    TARGETS=(
        "macos-arm64|build/release/macos-arm64/logamuffin|tar.gz"
        "macos-x86_64|build/release/macos-x86_64/logamuffin|tar.gz"
        "windows-x86_64|build/release/windows-x86_64/logamuffin.exe|zip"
        "windows-aarch64|build/release/windows-arm64/logamuffin.exe|zip"
        "linux-x86_64-musl|build/release/linux-x86_64/logamuffin|tar.gz"
        "linux-aarch64-musl|build/release/linux-arm64/logamuffin|tar.gz"
    )

    for target in "${TARGETS[@]}"; do
        IFS='|' read -r name binary format <<<"$target"

        if [[ ! -f "$SCRIPT_DIR/$binary" ]]; then
            echo "ERROR: Missing binary: $binary" >&2
            echo "Build all targets first, or check your build outputs." >&2
            exit 1
        fi
    done

    # ============================================================
    # Package
    # ============================================================

    mkdir -p -- "$DIST_PATH"
    rm -f -- "$DIST_PATH"/logamuffin-v"$VERSION"-* "$DIST_PATH"/SHA256SUMS

    echo "Packaging release assets..."
    echo

    for target in "${TARGETS[@]}"; do
        IFS='|' read -r name binary format <<<"$target"

        filename="logamuffin-v${VERSION}-${name}.${format}"
        output="$DIST_PATH/$filename"

        echo "------------------------------------------------------------"
        echo "Target:  $name"
        echo "Binary:  $binary"
        echo "Archive: $filename"
        echo "------------------------------------------------------------"

        if [[ "$format" == "zip" ]]; then
            # Flat ZIP: logamuffin.exe at the archive root.
            zip -j -q "$output" "$SCRIPT_DIR/$binary"
        else
            # Flat tarball: logamuffin at the archive root.
            tar -czf "$output" -C "$SCRIPT_DIR/$(dirname "$binary")" \
                "$(basename "$binary")"
        fi

        echo "SUCCESS: $filename"
        echo
    done

    # ============================================================
    # Checksums
    # ============================================================

    echo "Generating SHA-256 checksums..."

    (
        cd "$DIST_PATH"

        shopt -s nullglob
        archives=(logamuffin-v"$VERSION"-*.tar.gz logamuffin-v"$VERSION"-*.zip)

        if [[ "$HASH_COMMAND" == "shasum" ]]; then
            shasum -a 256 "${archives[@]}" >SHA256SUMS
        else
            sha256sum "${archives[@]}" >SHA256SUMS
        fi
    )

    # ============================================================
    # Summary
    # ============================================================

    echo
    echo "============================================================"
    echo " Release packaging complete!"
    echo "============================================================"
    echo "Version: $VERSION"
    echo "Assets:  $DIST_PATH"
    echo

    ls -lh "$DIST_PATH"/logamuffin-v"$VERSION"-* "$DIST_PATH/SHA256SUMS"

    echo
    echo "Upload these files to GitHub Release v$VERSION."
    echo "============================================================"
)
