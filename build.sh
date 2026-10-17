#!/usr/bin/env bash
set -euo pipefail

(
    # ============================================================
    # Configuration
    # ============================================================

    # Build mode: release or debug
    BUILD_MODE="${BUILD_MODE:-release}"

    # Common compiler settings
    CXX_STANDARD="c++20"

    # Compilers
    NATIVE_CXX="${NATIVE_CXX:-clang++}"
    ZIG_CXX="${ZIG_CXX:-zig c++}"

    # Output behavior
    CLEAN_BUILD="${CLEAN_BUILD:-true}"

    # Build flags
    case "$BUILD_MODE" in
    release)
        BUILD_FLAGS=(-O2 -DNDEBUG)
        ;;
    debug)
        BUILD_FLAGS=(-O0 -g3 -DDEBUG)
        ;;
    *)
        echo "ERROR: Invalid BUILD_MODE='$BUILD_MODE' (use release or debug)" >&2
        exit 2
        ;;
    esac

    # ============================================================
    # Setup
    # ============================================================

    SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    cd "$SCRIPT_DIR"

    echo "============================================================"
    echo " Logger Build"
    echo "============================================================"
    echo "Mode:       $BUILD_MODE"
    echo "C++:        $CXX_STANDARD"
    echo "Flags:      ${BUILD_FLAGS[*]}"
    echo "Clean:      $CLEAN_BUILD"
    echo "============================================================"
    echo

    # ============================================================
    # Build helper
    # ============================================================

    build_target() {
        local name="$1"
        local output_dir="$2"
        local compiler="$3"
        local target="${4:-}"
        local output="$output_dir/logger"

        if [[ "$output_dir" == windows-* ]]; then
            output="${output}.exe"
        fi

        echo "------------------------------------------------------------"
        echo "Building: $name"
        echo "Output:   $output"
        echo "------------------------------------------------------------"

        if [[ "$CLEAN_BUILD" == "true" ]]; then
            rm -rf -- "$output_dir"
        fi
        mkdir -p -- "$output_dir"

        if [[ -n "$target" ]]; then
            # Zig cross-compilation
            # compiler is intentionally invoked through the shell below
            zig c++ \
                -target "$target" \
                "-std=$CXX_STANDARD" \
                "${BUILD_FLAGS[@]}" \
                logger.cpp \
                -o "$output"
        else
            # Native compilation
            "$compiler" \
                "-std=$CXX_STANDARD" \
                "${BUILD_FLAGS[@]}" \
                logger.cpp \
                -o "$output"
        fi

        echo "SUCCESS: $name"
        echo
    }

    # ============================================================
    # Targets
    # ============================================================

    build_target "macOS x86-64" \
        "macos-x86_64" "$NATIVE_CXX"

    build_target "Windows x86-64 (GNU)" \
        "windows-x86_64" "" "x86_64-windows-gnu"

    build_target "Linux x86-64 (musl)" \
        "linux-x86_64" "" "x86_64-linux-musl"

    build_target "Linux ARM64 (musl)" \
        "linux-arm64" "" "aarch64-linux-musl"

    # ============================================================
    # Summary
    # ============================================================

    echo "============================================================"
    echo " Build complete!"
    echo "============================================================"
    echo "Mode: $BUILD_MODE"
    echo
    find macos-x86_64 windows-x86_64 linux-x86_64 linux-arm64 \
        -maxdepth 1 -type f -name 'logger*' -print
    echo
)
