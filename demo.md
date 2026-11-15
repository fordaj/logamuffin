# Feature demo (`demo.sh`)

`demo.sh` is a self-contained showcase of every user-facing capability documented in [README.md](README.md): version/help, severity levels and colors, aliases and numeric levels, threshold filtering, stdout vs stderr routing, the environment surface, custom categories, JSON config and precedence, persistent file logging, concurrent writes, shell patterns, and the Bash `log` helper. Section order follows the README.

## Running it

```bash
./demo.sh
```

There is intentionally no `set -e`: several sections trigger expected failures (unknown level, bad config) to show exit codes, so aborting on error would defeat the demo.

The script locates a `logamuffin` binary by `command -v`, then `build/debug/logamuffin` (the [run_tests.md](run_tests.md) output), then `build/{release,debug}/<platform>/logamuffin[.exe]` (the [scripts/build.md](scripts/build.md) output). If none is found it exits with an error explaining how to build one.

Demo state is isolated: host `LOG_*`/`LOGAMUFFIN_*` variables are unset up front and output goes to a `mktemp -d` directory that is removed on exit, so running the demo never touches your tree or your real `.logs/log.log`.

Re-run it after any change to compare outputs for regressions; the README's `image.png` is a capture of a demo run.

## Related pages

- [README.md](README.md) — the user documentation the demo mirrors
- [log.md](log.md) — the `log` helper demonstrated in the final sections
- [run_tests.md](run_tests.md) — builds `build/debug/logamuffin` used by the demo
- [scripts/build.md](scripts/build.md) — builds the per-platform binaries
