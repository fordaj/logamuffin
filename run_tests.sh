#!/usr/bin/env bash
# Run the full Logamuffin test suite: C++ unit tests + CLI integration tests.
#
# Usage:
#   ./run_tests.sh
#   ./run_tests.sh --help
#   BUILD_MODE=debug ./run_tests.sh
#   SANITIZE=1 ./run_tests.sh
#   ./run_tests.sh --skip-integration
#   ./run_tests.sh --skip-unit
#   ./run_tests.sh --only-unit
#   ./run_tests.sh --only-integration
#   LOG_BIN=/path/to/logamuffin ./run_tests.sh --only-integration
#
# Environment:
#   CXX         C++ compiler to use (default: auto-detect clang++, g++, c++)
#   BUILD_MODE  release (default) or debug
#   SANITIZE    1 to build with AddressSanitizer + UndefinedBehaviorSanitizer
#   PYTHON      Python interpreter for integration tests (default: python3 or python)
#   LOG_BIN     Executable to test (default: freshly built ./build/logamuffin)
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RUN_UNIT=true
RUN_INTEGRATION=true

usage() {
    cat <<'EOF'
Usage: ./run_tests.sh [options]

Options:
  --skip-unit        Skip C++ unit tests (doctest)
  --skip-integration Skip Python CLI integration tests
  --only-unit        Run only unit tests
  --only-integration Run only integration tests
  --help             Show this help

Environment:
  CXX         C++ compiler (auto-detect clang++, g++, c++)
  BUILD_MODE  release (default) or debug
  SANITIZE    1 = enable ASan + UBSan
  PYTHON      Python interpreter (default: python3)
  LOG_BIN     Executable to test (default: ./build/logamuffin)
EOF
}

for arg in "$@"; do
    case "$arg" in
    --skip-unit)        RUN_UNIT=false ;;
    --skip-integration) RUN_INTEGRATION=false ;;
    --only-unit)        RUN_UNIT=true; RUN_INTEGRATION=false ;;
    --only-integration) RUN_UNIT=false; RUN_INTEGRATION=true ;;
    --help|-h)          usage; exit 0 ;;
    *) echo "ERROR: Unknown option '$arg'" >&2; usage >&2; exit 2 ;;
    esac
done

BUILD_MODE="${BUILD_MODE:-release}"
SANITIZE="${SANITIZE:-0}"
PYTHON="${PYTHON:-}"

case "$BUILD_MODE" in
release) BUILD_FLAGS=(-O2 -DNDEBUG) ;;
debug)   BUILD_FLAGS=(-O0 -g3 -DDEBUG) ;;
*) echo "ERROR: Invalid BUILD_MODE='$BUILD_MODE' (use release or debug)" >&2; exit 2 ;;
esac

if [[ "$SANITIZE" == "1" ]]; then
    # shellcheck disable=SC2054 # comma is part of the sanitizer flag
    BUILD_FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

# Detect compiler (skip detection when only running integration tests
# against a prebuilt LOG_BIN).
DETECT_CXX=""
if [[ "$RUN_UNIT" == "true" || -n "${LOG_BIN:-}" ]]; then
    if [[ -n "${CXX:-}" ]]; then
        DETECT_CXX="$CXX"
    elif command -v clang++ >/dev/null 2>&1; then
        DETECT_CXX="clang++"
    elif command -v g++ >/dev/null 2>&1; then
        DETECT_CXX="g++"
    elif command -v c++ >/dev/null 2>&1; then
        DETECT_CXX="c++"
    else
        echo "ERROR: No C++ compiler found (need clang++, g++, or c++)" >&2
        exit 1
    fi
fi

# Detect Python.
if [[ -z "$PYTHON" ]]; then
    if command -v python3 >/dev/null 2>&1; then
        PYTHON="python3"
    elif command -v python >/dev/null 2>&1; then
        PYTHON="python"
    else
        echo "ERROR: No Python interpreter found (need python3 or python)" >&2
        exit 1
    fi
fi

echo "============================================================"
echo " Logamuffin Test Suite"
echo "============================================================"
echo "Build mode:  $BUILD_MODE"
if [[ -n "$DETECT_CXX" ]]; then
    echo "Compiler:    $DETECT_CXX"
fi
echo "Flags:       ${BUILD_FLAGS[*]:-}"
echo "Unit tests:  $RUN_UNIT"
echo "Integration: $RUN_INTEGRATION"
echo "============================================================"
echo

failures=0

if [[ "$RUN_UNIT" == "true" ]]; then
    echo "------------------------------------------------------------"
    echo " Building unit tests"
    echo "------------------------------------------------------------"
    mkdir -p build/tests
    # shellcheck disable=SC2086
    if ! "$DETECT_CXX" -std=c++20 "${BUILD_FLAGS[@]}" -I. \
        tests/unit/test_logamuffin.cpp -o build/tests/test_logamuffin; then
        echo "ERROR: Unit test build failed" >&2
        exit 1
    fi
    echo "SUCCESS: unit test build"
    echo
    echo "------------------------------------------------------------"
    echo " Running unit tests (doctest)"
    echo "------------------------------------------------------------"
    if build/tests/test_logamuffin; then
        echo "SUCCESS: unit tests"
    else
        echo "FAIL: unit tests" >&2
        failures=$((failures + 1))
    fi
    echo
fi

if [[ "$RUN_INTEGRATION" == "true" ]]; then
    echo "------------------------------------------------------------"
    echo " Building logamuffin executable (native)"
    echo "------------------------------------------------------------"
    mkdir -p build
    case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*) EXE_BIN="build/logamuffin.exe" ;;
    *) EXE_BIN="build/logamuffin" ;;
    esac
    # shellcheck disable=SC2086
    if ! "$DETECT_CXX" -std=c++20 "${BUILD_FLAGS[@]}" \
        logamuffin.cpp -o "$EXE_BIN"; then
        echo "ERROR: logamuffin build failed" >&2
        exit 1
    fi
    echo "SUCCESS: logamuffin build -> $EXE_BIN"
    echo
    echo "------------------------------------------------------------"
    echo " Running CLI integration tests (Python unittest)"
    echo "------------------------------------------------------------"
    TEST_BIN="${LOG_BIN:-$EXE_BIN}"
    echo "Executable: $TEST_BIN"
    echo
    if "$PYTHON" tests/integration/test_cli.py --executable "$TEST_BIN"; then
        echo "SUCCESS: integration tests"
    else
        echo "FAIL: integration tests" >&2
        failures=$((failures + 1))
    fi
    echo
fi

echo "============================================================"
if ((failures > 0)); then
    echo " Test suite FAILED ($failures stage(s) failed)"
    echo "============================================================"
    exit 1
else
    echo " Test suite PASSED"
    echo "============================================================"
fi
