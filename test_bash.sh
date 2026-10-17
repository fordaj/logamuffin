#!/usr/bin/env bash
set -euo pipefail

# --- Source Scripts --- #
(return 0 2>/dev/null) && sourced=true || sourced=false
if ! "${sourced}"; then
    source Utilities/setup_sh.sh
fi

for i in {1..100}; do
    log $LOG_LEVEL_INFO "Benchmark message $i" &
done
