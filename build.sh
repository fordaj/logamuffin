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

    # Translation units (split from the former 1955-line logamuffin.cpp;
    # see docs/TESTING.md for the module layout).
    SOURCES=(logamuffin.cpp text_utils.cpp json_parser.cpp color.cpp
             log_levels.cpp config.cpp platform.cpp)

    # ============================================================
    # Setup
    # ============================================================

    SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    cd "$SCRIPT_DIR"

    # All build outputs live under build/<mode>/<platform>/ so release
    # and debug binaries never collide and the repo root stays clean
    # (issue #1). For example: build/release/macos-arm64/logamuffin,
    # build/debug/linux-x86_64/logamuffin.
    BUILD_ROOT="build/$BUILD_MODE"

    echo "============================================================"
    echo " Logamuffin Build"
    echo "============================================================"
    echo "Mode:       $BUILD_MODE"
    echo "C++:        $CXX_STANDARD"
    echo "Flags:      ${BUILD_FLAGS[*]}"
    echo "Clean:      $CLEAN_BUILD"
    echo "Root:       $BUILD_ROOT"
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

        if [[ "$output_dir" == *windows-* ]]; then
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
                "${SOURCES[@]}" \
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
                "${SOURCES[@]}" \
                -o "$output"
        fi

        echo "SUCCESS: $name"
        echo
    }

    # ============================================================
    # Targets
    # ============================================================
    build_target "macOS Intel x86-64" \
        "$BUILD_ROOT/macos-x86_64" "$NATIVE_CXX" "" "x86_64"

    build_target "macOS Apple Silicon ARM64" \
        "$BUILD_ROOT/macos-arm64" "$NATIVE_CXX" "" "arm64"

    build_target "Windows x86-64 (GNU)" \
        "$BUILD_ROOT/windows-x86_64" "" "x86_64-windows-gnu"

    build_target "Windows ARM64 (GNU)" \
        "$BUILD_ROOT/windows-arm64" "" "aarch64-windows-gnu"

    build_target "Linux x86-64 (musl)" \
        "$BUILD_ROOT/linux-x86_64" "" "x86_64-linux-musl"

    build_target "Linux ARM64 (musl)" \
        "$BUILD_ROOT/linux-arm64" "" "aarch64-linux-musl"

    # ============================================================
    # Summary
    # ============================================================

    echo "============================================================"
    echo " Build complete!"
    echo "============================================================"
    echo "Mode: $BUILD_MODE"
    echo
    find "$BUILD_ROOT/macos-x86_64" "$BUILD_ROOT/macos-arm64" \
        "$BUILD_ROOT/windows-x86_64" "$BUILD_ROOT/windows-arm64" \
        "$BUILD_ROOT/linux-x86_64" "$BUILD_ROOT/linux-arm64" \
        -maxdepth 1 -type f -name 'logamuffin*' -print
    echo

)
