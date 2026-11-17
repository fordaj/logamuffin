# Regenerating the version header (`tools/generate_version.sh`)

```mermaid
flowchart LR
    GV("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>generate_version.sh") --> Ver("<img src='barnacle:tag.svg' width='20' alt=''/><br/>versioner --format shell<br/>against the repo")
    Ver --> Check{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>validate<br/>5 components + tag matches"}}
    Check -->|fail| Err("<img src='barnacle:circle-exclamation-point.svg' width='20' alt=''/><br/>exit 1")
    Check -->|ok| Out("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>.version/version.hpp<br/>LOGAMUFFIN_VERSION")
    Out --> Consumers("<img src='barnacle:wrench.svg' width='20' alt=''/><br/><u>build.sh · release.sh<br/>run_tests.sh</u>")
    click Consumers href "../scripts/build.sh.md"
    class GV shFile
    class Ver tagNode
    class Check hexNode
    class Err errNode
    class Out goFile
    classDef shFile fill:#abe07a,stroke:#3f6212,color:#16280a
    classDef tagNode fill:#f6b8d4,stroke:#be185d,color:#500724
    classDef hexNode fill:#ffe08a,stroke:#a16207,color:#422006
    classDef errNode fill:#f6b8b8,stroke:#b91c1c,color:#450a0a
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
```

`tools/generate_version.sh` regenerates `.version/version.hpp` from git history via the bundled [Versioner](versioner/update.sh.md) binary. Build scripts call it before compiling so `logamuffin --version` always reflects the current `HEAD`. The `.version/` directory is git-ignored and must not be committed. Host platform detection and bundled-binary resolution are shared with the release tooling via [host_platform.sh.md](host_platform.sh.md) (`host_platform` / `locate_versioner` / `parse_shell_export`).

## Usage

```bash
tools/generate_version.sh          # regenerate .version/version.hpp
tools/generate_version.sh --help   # usage
```

Prerequisites: `git` (versioner shells out to it) and a bundled versioner binary for this host (or `VERSIONER_BIN` set to a prebuilt one). Compile with `-I.version` so `#include "version.hpp"` resolves.

## What it writes

`.version/version.hpp` defines `LOGAMUFFIN_VERSION_BRANCH`, `_MAJOR`, `_MINOR`, `_REVISION`, `_PATCH`, plus `LOGAMUFFIN_VERSION` (five-part string, e.g. `0.1.3.0.34`) and `LOGAMUFFIN_VERSION_TAG` (`v0.1.3.0.34`). The script runs `versioner --repo-dir <root> --format shell`, validates the five components and that the tag equals `v<VERSION>`, and writes the header atomically.

Environment: `VERSIONER_BIN` (prebuilt binary override), `OUTPUT_DIR` (default `<root>/.version`). Unknown arguments exit 2; validation failures exit 1.

Called by [../scripts/build.sh.md](../scripts/build.sh.md), [../scripts/release.sh.md](../scripts/release.sh.md), and [../run_tests.sh.md](../scripts/run_tests.sh.md) — all regenerate before building, so the header and binary never disagree.

## Related pages

- [versioner/update.sh.md](versioner/update.sh.md) — the bundled binary and how to refresh it
- [host_platform.sh.md](host_platform.sh.md) — the shared host/versioner helpers this script sources
- [../scripts/build.sh.md](../scripts/build.sh.md) — caller
- [../README.md](../README.md) — "Versioning" section
