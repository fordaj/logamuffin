#!/usr/bin/env bash
# Run the complete logamuffin test suite (unit + CLI integration).
#
#   ./run_tests.sh                      # full suite (dev build)
#   ./run_tests.sh --executable ./build/release/macos-arm64/logamuffin
#   ./run_tests.sh --skip-unit          # integration only
#   ./run_tests.sh --skip-integration   # unit only
#   ./run_tests.sh --help
#
# The integration suite (Python standard library only) can also target a
# packaged release binary on any platform:
#   LOG_BIN=/path/to/logamuffin python3 tests/integration/test_cli.py
#
# See docs/TESTING.md for the testing architecture.

set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CXX="${CXX:-clang++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    CXX="c++"
fi
CXXFLAGS=(-std=c++20 -O0 -g3 -DDEBUG)
UNIT_BIN="build/debug/tests/unit/test_logamuffin"
NATIVE_BIN="build/debug/logamuffin"
EXECUTABLE=""

# Translation units (split from the former 1955-line logamuffin.cpp).
SOURCES=(logamuffin.cpp logamuffin_core.cpp text_utils.cpp json_parser.cpp color.cpp
         log_levels.cpp config.cpp platform.cpp)
UNIT_SOURCES=(logamuffin_core.cpp text_utils.cpp)

SKIP_UNIT="false"
SKIP_INTEGRATION="false"
CLEAN="false"
VERBOSE="false"

usage() {
    cat <<'EOF'
Usage: ./run_tests.sh [--executable PATH] [--skip-unit] [--skip-integration] [--clean] [--verbose]

Runs the C++ unit tests and the cross-platform CLI integration tests.

Options:
  --executable PATH    Test this binary instead of a fresh dev build
                       (e.g. a packaged release binary). Unit tests still run.
  --skip-unit          Skip C++ unit tests.
  --skip-integration   Skip CLI integration tests.
  --clean              Remove previous test build outputs before building.
  --verbose            Verbose output for the integration suite.
  --help               Show this help.

Environment:
  CXX                  C++ compiler (default: clang++, falls back to c++).

Examples:
  ./run_tests.sh
  ./run_tests.sh --executable ./build/release/macos-arm64/logamuffin
  LOG_BIN=./logamuffin python3 tests/integration/test_cli.py

On Windows, run the integration suite directly:
  LOG_BIN=logamuffin.exe python tests\integration\test_cli.py
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --executable)
            EXECUTABLE="${2:-}"
            shift 2
            ;;
        --executable=*)
            EXECUTABLE="${1#--executable=}"
            shift
            ;;
        --skip-unit)
            SKIP_UNIT="true"; shift ;;
        --skip-integration)
            SKIP_INTEGRATION="true"; shift ;;
        --clean)
            CLEAN="true"; shift ;;
        --verbose)
            VERBOSE="true"; shift ;;
        -v)
            VERBOSE="true"; shift ;;
        --help|-h)
            usage; exit 0 ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

echo "============================================================"
echo " Logamuffin Test Suite"
echo "============================================================"
echo "Compiler:     $CXX ${CXXFLAGS[*]}"
if [[ -n "$EXECUTABLE" ]]; then
    echo "Executable:   $EXECUTABLE (provided)"
else
    echo "Executable:   $NATIVE_BIN (fresh dev build)"
fi
echo "Skip unit:    $SKIP_UNIT"
echo "Skip integ:   $SKIP_INTEGRATION"
echo "============================================================"
echo

# ------------------------------------------------------------------
# Preflight
# ------------------------------------------------------------------
if [[ -z "${EXECUTABLE}" ]] && ! command -v "$CXX" >/dev/null 2>&1; then
    echo "ERROR: C++ compiler not found: $CXX" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found (required for CLI integration tests)" >&2
    exit 1
fi
if [[ ! -f "tools/doctest/v2.5.3/doctest.h" ]]; then
    echo "ERROR: doctest header not found at tools/doctest/v2.5.3/doctest.h" >&2
    exit 1
fi
if [[ ! -f "logamuffin.cpp" || ! -f "logamuffin.hpp" ]]; then
    echo "ERROR: logamuffin.cpp / logamuffin.hpp not found (run from repo root)" >&2
    exit 1
fi
if [[ -n "$EXECUTABLE" && ! -x "$EXECUTABLE" && ! -f "$EXECUTABLE" ]]; then
    echo "ERROR: executable not found: $EXECUTABLE" >&2
    exit 1
fi

if [[ "$CLEAN" == "true" ]]; then
    rm -rf -- build/debug/tests build/debug/logamuffin
fi

UNIT_STATUS="skipped"
INTEG_STATUS="skipped"
EXIT_CODE=0

# ------------------------------------------------------------------
# Unit tests
# ------------------------------------------------------------------
if [[ "$SKIP_UNIT" != "true" ]]; then
    echo "------------------------------------------------------------"
    echo " Building + running C++ unit tests"
    echo "------------------------------------------------------------"
    mkdir -p "$(dirname "$UNIT_BIN")"
    if "$CXX" "${CXXFLAGS[@]}" -I. -Itools/doctest/v2.5.3 \
        tests/unit/test_logamuffin.cpp "${UNIT_SOURCES[@]}" -o "$UNIT_BIN"; then
        echo "Unit binary: $UNIT_BIN"
        echo
        if "$UNIT_BIN"; then
            UNIT_STATUS="passed"
        else
            UNIT_STATUS="FAILED"
            EXIT_CODE=1
        fi
    else
        UNIT_STATUS="BUILD FAILED"
        EXIT_CODE=1
    fi
    echo
fi

# ------------------------------------------------------------------
# Integration tests
# ------------------------------------------------------------------
if [[ "$SKIP_INTEGRATION" != "true" ]]; then
    echo "------------------------------------------------------------"
    echo " Building + running CLI integration tests"
    echo "------------------------------------------------------------"

    BIN_PATH="$NATIVE_BIN"
    if [[ -n "$EXECUTABLE" ]]; then
        BIN_PATH="$EXECUTABLE"
        echo "Using provided executable: $BIN_PATH"
    else
        mkdir -p "$(dirname "$NATIVE_BIN")"
        if "$CXX" "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$NATIVE_BIN"; then
            echo "Built native binary: $NATIVE_BIN"
        else
            INTEG_STATUS="BUILD FAILED"
            EXIT_CODE=1
        fi
    fi
    echo

    if [[ "$INTEG_STATUS" != "BUILD FAILED" ]]; then
        if [[ "$VERBOSE" == "true" ]]; then
            if python3 tests/integration/test_cli.py --executable "$BIN_PATH" -v; then
                INTEG_STATUS="passed"
            else
                INTEG_STATUS="FAILED"
                EXIT_CODE=1
            fi
        else
            if python3 tests/integration/test_cli.py --executable "$BIN_PATH"; then
                INTEG_STATUS="passed"
            else
                INTEG_STATUS="FAILED"
                EXIT_CODE=1
            fi
        fi
    fi
    echo
fi

# ------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------
echo "============================================================"
echo " Results"
echo "============================================================"
echo "Unit tests:        $UNIT_STATUS"
echo "Integration tests: $INTEG_STATUS"
echo "============================================================"

exit "$EXIT_CODE"
