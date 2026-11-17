# Feature demo (`scripts/demo.sh`)

```mermaid
flowchart TD
    Demo("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>demo.sh") --> Find{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>locate a binary"}}
    Find -->|command -v| OnPath("<img src='barnacle:check.svg' width='20' alt=''/><br/>logamuffin on PATH")
    Find -->|build/debug| Debug("<img src='barnacle:binary.svg' width='20' alt=''/><br/><u>build/debug/logamuffin</u>")
    Find -->|platform output| Rel("<img src='barnacle:binary.svg' width='20' alt=''/><br/><u>build/mode/platform/logamuffin</u>")
    Find -->|none found| Err("<img src='barnacle:circle-exclamation-point.svg' width='20' alt=''/><br/>error + how to build")
    Demo --> Iso("<img src='barnacle:folder.svg' width='20' alt=''/><br/>mktemp -d sandbox<br/>LOG_* scrubbed")
    Iso --> Run("<img src='barnacle:play.svg' width='20' alt=''/><br/>every capability,<br/>in README order")
    click Rel href "build.sh.md"
    click Debug href "run_tests.sh.md"
    class Demo shFile
    class Find hexNode
    class OnPath checkNode
    class Debug,Rel goFile
    class Err errNode
    class Iso folder
    classDef shFile fill:#abe07a,stroke:#3f6212,color:#16280a
    classDef hexNode fill:#ffe08a,stroke:#a16207,color:#422006
    classDef checkNode fill:#9fe8c9,stroke:#0f766e,color:#052e22
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
    classDef errNode fill:#f6b8b8,stroke:#b91c1c,color:#450a0a
    classDef folder fill:#f0c558,stroke:#8a6d1c,color:#332a05
```

`scripts/demo.sh` is a self-contained showcase of every user-facing capability documented in [README.md](../README.md): version/help, severity levels and colors, aliases and numeric levels, threshold filtering, stdout vs stderr routing, the environment surface, custom categories, JSON config and precedence, persistent file logging, concurrent writes, shell patterns, and the Bash `log` helper. Section order follows the README.

## Running it

```bash
./scripts/demo.sh
```

There is intentionally no `set -e`: several sections trigger expected failures (unknown level, bad config) to show exit codes, so aborting on error would defeat the demo.

The script locates a `logamuffin` binary by `command -v`, then `build/debug/logamuffin` (the [scripts/run_tests.sh.md](run_tests.sh.md) output), then `build/{release,debug}/<platform>/logamuffin[.exe]` (the [scripts/build.sh.md](build.sh.md) output; the platform name comes from the shared `tools/host_platform.sh` helpers). If none is found it exits with an error explaining how to build one.

Demo state is isolated: host `LOG_*`/`LOGAMUFFIN_*` variables are unset up front and output goes to a `mktemp -d` directory that is removed on exit, so running the demo never touches your tree or your real `.logs/log.log`.

Re-run it after any change to compare outputs for regressions; the README's `image.png` is a capture of a demo run.

## Related pages

- [README.md](../README.md) — the user documentation the demo mirrors
- [scripts/log.sh.md](log.sh.md) — the `log` helper demonstrated in the final sections
- [scripts/run_tests.sh.md](run_tests.sh.md) — builds `build/debug/logamuffin` used by the demo
- [scripts/build.sh.md](build.sh.md) — builds the per-platform binaries
