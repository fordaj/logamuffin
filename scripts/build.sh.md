# Cross-platform build (`scripts/build.sh`)

```mermaid
flowchart LR
    Mode{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/>BUILD_MODE"}} --> Rel("<img src='barnacle:settings.svg' width='20' alt=''/><br/>release · -O2 -DNDEBUG")
    Mode --> Dbg("<img src='barnacle:settings.svg' width='20' alt=''/><br/>debug · -O0 -g3 -DDEBUG")
    Mode --> Bad("<img src='barnacle:circle-exclamation-point.svg' width='20' alt=''/><br/>anything else · exit 2")
    Src("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>tools/sources.sh<br/>SOURCES — 8 units") --> Compile
    Gen("<img src='barnacle:terminal.svg' width='20' alt=''/><br/><u>tools/generate_version.sh<br/>regenerates .version/</u>") --> Compile
    Rel --> Compile("<img src='barnacle:hammer.svg' width='20' alt=''/><br/>compile per target<br/>native clang++ · Zig musl")
    Dbg --> Compile
    Compile --> Out("<img src='barnacle:binary.svg' width='20' alt=''/><br/>build/mode/platform/<br/>logamuffin[.exe]")
    click Gen href "../README.md"
    class Mode hexNode
    class Src,Gen shFile
    class Bad errNode
    class Out goFile
    classDef hexNode fill:#ffe08a,stroke:#a16207,color:#422006
    classDef shFile fill:#abe07a,stroke:#3f6212,color:#16280a
    classDef errNode fill:#f6b8b8,stroke:#b91c1c,color:#450a0a
    classDef goFile fill:#8fd8ef,stroke:#0277a8,color:#08303d
```

`scripts/build.sh` builds the six release targets: macOS x86-64 and arm64 (native `clang++`), and Windows x86-64/arm64 + Linux x86-64/arm64 (musl) via Zig (`zig c++ -target …`). Mode comes from `BUILD_MODE` — `release` (default, `-O2 -DNDEBUG`) or `debug` (`-O0 -g3 -DDEBUG`); anything else exits 2. Native compiler can be overridden with `NATIVE_CXX`, Zig with `ZIG_CXX`, and clean rebuilds with `CLEAN_BUILD=false`.

## What it does

1. `cd`s to the repo root and sources `tools/sources.sh.md` for the translation-unit list.
2. Regenerates `.version/version.hpp` via `tools/generate_version.sh.md` so `--version` always reflects `HEAD` (`.version/` is git-ignored).
3. Compiles each target into `build/<mode>/<platform>/logamuffin[.exe]` — release and debug outputs never collide, and the repo root stays clean.

```bash
BUILD_MODE=debug scripts/build.sh
BUILD_MODE=release scripts/build.sh
```

Consumers: `scripts/release.sh` builds release targets through this script; [../demo.sh.md](demo.sh.md) and `scripts/log.sh` look for binaries under `build/<mode>/<platform>/`.

## Outputs

`build/<mode>/macos-x86_64/`, `…/macos-arm64/`, `…/windows-x86_64/`, `…/windows-arm64/`, `…/linux-x86_64/`, `…/linux-arm64/`, each holding `logamuffin[.exe]`. `build/` is git-ignored.

## Related pages

- `tools/sources.sh.md` — the `SOURCES` array compiled here
- `tools/generate_version.sh.md` — version header regeneration
- [release.sh.md](release.sh.md) — packages these binaries into release archives
- [../README.md](../README.md) — "Building from source"
