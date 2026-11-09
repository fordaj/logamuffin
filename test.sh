#!/usr/bin/env bash
# Deprecated shim — canonical suite is ./run_tests.sh / tests/integration/test_cli.py.
#
# The Python integration suite (via run_tests.sh) supersedes this legacy
# Bash suite: isolated tmp dirs, per-test reporting, and broader coverage.
# This wrapper forwards to it with the same exit code for compatibility.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
echo "test.sh is deprecated; running canonical integration suite via ./run_tests.sh --skip-unit" >&2
exec "$SCRIPT_DIR/run_tests.sh" --skip-unit "$@"
