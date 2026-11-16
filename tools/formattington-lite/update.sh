#!/usr/bin/env bash
# update.sh — refresh bundled formattington-lite binaries from fordaj/formattington releases.
#
# Thin wrapper around tools/update-bundle.sh (the shared implementation);
# see that file and tools/update-bundle.sh.md for the full behavior.
# Only `-lite` archives are consumed; NOTICE.md and LICENSES/ are kept
# alongside the binary when present.
#
# Usage:
#   tools/formattington-lite/update.sh              # download latest, replace old versions
#   tools/formattington-lite/update.sh --dry-run    # resolve tag + list assets, change nothing
#   tools/formattington-lite/update.sh --help       # usage
#
# Env:
#   FORMATTINGTON_REPO  Repository slug (default: fordaj/formattington)
#   FORMATTINGTON_TAG   Pinned tag (default: latest release)

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

export TOOL_NAME="formattington-lite"
export TOOL_BIN="formattington"
export TOOL_REPO_DEFAULT="fordaj/formattington"
export TOOL_ENV_PREFIX="FORMATTINGTON"
export TOOL_ASSET_SUFFIX="-lite"
export TOOL_EXTRA_FILES="NOTICE.md"
export TOOL_EXTRA_DIRS="LICENSES"
export TOOL_EXTRAS_NOTE="Only the binary is kept, plus NOTICE.md and LICENSES/."

exec "$SCRIPT_DIR/../update-bundle.sh" "$@"
