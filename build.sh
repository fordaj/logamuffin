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
    echo " Logamuffin Build"
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
        local arch="${5:-}"
        local output="$output_dir/logamuffin"

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
            zig c++ \
                -target "$target" \
                "-std=$CXX_STANDARD" \
                "${BUILD_FLAGS[@]}" \
                logamuffin.cpp \
                -o "$output"
        else
            local -a arch_flags=()
            if [[ -n "$arch" ]]; then
                arch_flags=(-arch "$arch")
            fi

            "$compiler" \
                "${arch_flags[@]}" \
                "-std=$CXX_STANDARD" \
                "${BUILD_FLAGS[@]}" \
                logamuffin.cpp \
                -o "$output"
        fi

        echo "SUCCESS: $name"
        echo
    }

    # ============================================================
    # Targets
    # ============================================================
    build_target "macOS Intel x86-64" \
        "macos-x86_64" "$NATIVE_CXX" "" "x86_64"

    build_target "macOS Apple Silicon ARM64" \
        "macos-arm64" "$NATIVE_CXX" "" "arm64"

    build_target "Windows x86-64 (GNU)" \
        "windows-x86_64" "" "x86_64-windows-gnu"

    build_target "Windows ARM64 (GNU)" \
        "windows-arm64" "" "aarch64-windows-gnu"

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
    find macos-x86_64 macos-arm64 windows-x86_64 linux-x86_64 linux-arm64 \
        -maxdepth 1 -type f -name 'logamuffin*' -print
    echo

)
