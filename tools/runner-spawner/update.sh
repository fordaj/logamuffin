#!/usr/bin/env bash
# update.sh — refresh bundled runner-spawner binaries from fordaj/runner-spawner releases.
#
# Thin wrapper around tools/update-bundle.sh (the shared implementation);
# see that file and tools/update-bundle.sh.md for the full behavior.
# Refuses to install binaries when the release ships no checksums.
#
# Usage:
#   tools/runner-spawner/update.sh              # download latest, replace old versions
#   tools/runner-spawner/update.sh --dry-run    # resolve tag + list assets, change nothing
#   tools/runner-spawner/update.sh --help       # usage
#
# Env:
#   RUNNER_SPAWNER_TOOL_REPO  Repository slug (default: fordaj/runner-spawner)
#   RUNNER_SPAWNER_TAG        Pinned tag (default: latest release)

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

export TOOL_NAME="runner-spawner"
export TOOL_BIN="runner-spawner"
export TOOL_REPO_DEFAULT="fordaj/runner-spawner"
export TOOL_REPO_ENV="RUNNER_SPAWNER_TOOL_REPO"
export TOOL_TAG_ENV="RUNNER_SPAWNER_TAG"
export TOOL_CHECKSUMS_REQUIRED=1
export TOOL_EXTRAS_NOTE="Only the binary is kept."

exec "$SCRIPT_DIR/../update-bundle.sh" "$@"
