#!/usr/bin/env bash
# update.sh — refresh bundled logamuffin binaries from fordaj/logamuffin releases.
#
# Thin wrapper around tools/update-bundle.sh (the shared implementation);
# see that file and tools/update-bundle.sh.md for the full behavior.
#
# Usage:
#   tools/logamuffin/update.sh              # download latest, replace old versions
#   tools/logamuffin/update.sh --dry-run    # resolve tag + list assets, change nothing
#   tools/logamuffin/update.sh --help       # usage
#
# Env:
#   LOGAMUFFIN_REPO  Repository slug (default: fordaj/logamuffin)
#   LOGAMUFFIN_TAG   Pinned tag (default: latest release)

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

export TOOL_NAME="logamuffin"
export TOOL_BIN="logamuffin"
export TOOL_REPO_DEFAULT="fordaj/logamuffin"
export TOOL_ENV_PREFIX="LOGAMUFFIN"
export TOOL_EXTRAS_NOTE="Only the binary is kept."

exec "$SCRIPT_DIR/../update-bundle.sh" "$@"
