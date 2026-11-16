#!/usr/bin/env bash
# update.sh — refresh bundled Versioner binaries from fordaj/Versioner releases.
#
# Thin wrapper around tools/update-bundle.sh (the shared implementation);
# see that file and tools/update-bundle.sh.md for the full behavior.
#
# Usage:
#   tools/versioner/update.sh              # download latest, replace old versions
#   tools/versioner/update.sh --dry-run    # resolve tag + list assets, change nothing
#   tools/versioner/update.sh --help       # usage
#
# Env:
#   VERSIONER_REPO  Repository slug (default: fordaj/Versioner)
#   VERSIONER_TAG   Pinned tag (default: latest release)

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

export TOOL_NAME="versioner"
export TOOL_BIN="versioner"
export TOOL_REPO_DEFAULT="fordaj/Versioner"
export TOOL_ENV_PREFIX="VERSIONER"
export TOOL_EXTRAS_NOTE="Only the binary is kept."

exec "$SCRIPT_DIR/../update-bundle.sh" "$@"
