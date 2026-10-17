#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Locate Logamuffin
# ============================================================

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if command -v logamuffin >/dev/null 2>&1; then
    LOGGER="$(command -v logamuffin)"
else
    case "$(uname -s):$(uname -m)" in
    Darwin:x86_64)
        BUILD_DIR="macos-x86_64"
        ;;
    Darwin:arm64)
        BUILD_DIR="macos-arm64"
        ;;
    MINGW*:x86_64 | MSYS*:x86_64 | CYGWIN*:x86_64)
        BUILD_DIR="windows-x86_64"
        ;;
    MINGW*:ARM64 | MSYS*:ARM64 | CYGWIN*:ARM64)
        BUILD_DIR="windows-arm64"
        ;;
    Linux:x86_64)
        BUILD_DIR="linux-x86_64"
        ;;
    Linux:aarch64 | Linux:arm64)
        BUILD_DIR="linux-arm64"
        ;;
    *)
        echo "ERROR: Unsupported platform: $(uname -s) $(uname -m)" >&2
        exit 1
        ;;
    esac

    LOGGER="$SCRIPT_DIR/$BUILD_DIR/logamuffin"

    # Windows build output uses the .exe suffix.
    case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        LOGGER="${LOGGER}.exe"
        ;;
    esac

    if [[ ! -x "$LOGGER" ]]; then
        echo "ERROR: Logamuffin not found on PATH or in expected build directory:" >&2
        echo "  $LOGGER" >&2
        echo "Build it with ./build.sh, or install it on PATH." >&2
        exit 1
    fi
fi

# ============================================================
# Showcase
# ============================================================

export LOG_LEVEL=TRACE
export LOG_COLOR=1
export LOG_FILE=/dev/null

printf '\n'
printf '  Logamuffin — Severity Color Showcase\n'
printf '  ====================================\n\n'

"$LOGGER" TRACE "Detailed execution trace"
"$LOGGER" DEBUG "Diagnostic information"
"$LOGGER" INFO "Everything is running normally"
"$LOGGER" WARNING "Something deserves your attention"
"$LOGGER" ERROR "An operation failed"
"$LOGGER" CRITICAL "A critical failure occurred"
"$LOGGER" ALWAYS "This message always gets through"

printf '\n'
