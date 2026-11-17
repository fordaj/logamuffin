# The Bash `log` helper (`scripts/log.sh`)

```mermaid
flowchart TD
    Call("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>log LEVEL MESSAGE") --> Resolve{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>binary resolution"}}
    Resolve -->|LOGAMUFFIN_BIN set| Bin("<img src='barnacle:binary.svg' width='20' alt=''/><br/>that binary")
    Resolve -->|command -v logamuffin| OnPath("<img src='barnacle:check.svg' width='20' alt=''/><br/>PATH")
    Resolve -->|repo build| Build("<img src='barnacle:binary.svg' width='20' alt=''/><br/><u>build/debug/logamuffin<br/>or build/mode/platform/</u>")
    Resolve -->|else| Bare("<img src='barnacle:binary.svg' width='20' alt=''/><br/>bare logamuffin")
    Call --> Src("<img src='barnacle:file-code.svg' width='20' alt=''/><br/>caller file : func : line<br/>passed as LOG_SOURCE")
    Src --> Bin
    OnPath --> Bin
    Build --> Bin
    click Build href "build.sh.md"
    class Call shFile
    class Resolve hexNode
    class Bin,Bare goFile
    class OnPath checkNode
    class Build goFile
    classDef shFile fill:#abe07a,stroke:#3f6212,color:#16280a
    classDef hexNode fill:#ffe08a,stroke:#a16207,color:#422006
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
    classDef checkNode fill:#9fe8c9,stroke:#0f766e,color:#052e22
```

`scripts/log.sh` defines a `log LEVEL [MESSAGE …]` Bash function that wraps the `logamuffin` executable so call sites stay terse: `log INFO "Starting"` instead of `logamuffin INFO "Starting"`. It resolves the caller's file, function, and line number and passes them to `logamuffin` as `LOG_SOURCE` (e.g. `my-script.sh:myfunc:42`), forwards `LEVEL` (a word such as `INFO` or a number such as `50`) and the message verbatim, and preserves `logamuffin`'s exit code.

## Install

```bash
source /path/to/log.sh   # or source ./scripts/log.sh from this repo
log INFO "Application started"
```

The function is also inlined in [README.md](../README.md) ("Bash helper"); the copy there is identical.

## Behavior notes

- Binary resolution happens at call time: `LOGAMUFFIN_BIN` if set, else `command -v logamuffin`, else the repo-local `build/debug/logamuffin` or `build/release/macos-arm64/logamuffin` if executable, else bare `logamuffin` on `PATH`.
- `LOG_LEVEL`, `LOG_FILE`, `LOG_COLOR`, `LOG_CONFIG`, `LOG_LEVELS`, and friends are inherited unchanged — export them once per script and `log` respects them. `LOG_SOURCE` is the exception: it is always set per call from the call site.
- Bash only (`BASH_SOURCE`/`FUNCNAME`/`BASH_LINENO`). For Zsh, capture the call site with `${(%):-%N}` / `$LINENO` instead — see README.

This is distinct from `scripts/entry.sh`'s `log()`, which is the same idea for this repository's own scripts (with a printf fallback when no binary is available); see [scripts/entry.sh.md](entry.sh.md).

## Related pages

- [logamuffin.cpp.md](../logamuffin.cpp.md) — the binary this wraps
- [scripts/entry.sh.md](entry.sh.md) — the repo-internal `log()` used by tooling
- [scripts/demo.sh.md](demo.sh.md) — demonstrates the helper end to end
- [README.md](../README.md)
