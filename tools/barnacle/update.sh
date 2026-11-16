#!/usr/bin/env bash
# update.sh — refresh bundled barnacle binaries from fordaj/barnacle releases.
#
# Thin wrapper around tools/update-bundle.sh (the shared implementation);
# see that file and tools/update-bundle.sh.md for the full behavior.
#
# Usage:
#   tools/barnacle/update.sh              # download latest, replace old versions
#   tools/barnacle/update.sh --dry-run    # resolve tag + list assets, change nothing
#   tools/barnacle/update.sh --help       # usage
#
# Env:
#   BARNACLE_REPO  Repository slug (default: fordaj/barnacle)
#   BARNACLE_TAG   Pinned tag (default: latest release)

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

export TOOL_NAME="barnacle"
export TOOL_BIN="barnacle"
export TOOL_REPO_DEFAULT="fordaj/barnacle"
export TOOL_ENV_PREFIX="BARNACLE"
export TOOL_EXTRA_FILES="LICENSE"
export TOOL_EXTRAS_NOTE="Only the binary is kept (plus the upstream MIT LICENSE)."

exec "$SCRIPT_DIR/../update-bundle.sh" "$@"
