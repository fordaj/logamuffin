# Printing the repository version (`scripts/version.sh`)

```mermaid
flowchart LR
    Bin{{"<img src='barnacle:hexagon.svg' width='20' alt=''/><br/><u>VERSIONER_BIN<br/>via env cascade</u>"}} --> V("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>version.sh")
    V -->|default| Text("<img src='barnacle:file-text.svg' width='20' alt=''/><br/>VERSION=… / TAG=…")
    V -->|shell| Shell("<img src='barnacle:terminal.svg' width='20' alt=''/><br/>export VERSION / TAG")
    V -->|json| Json("<img src='barnacle:braces.svg' width='20' alt=''/><br/>raw versioner JSON")
    Rel("<img src='barnacle:terminal.svg' width='20' alt=''/><br/><u>release.sh reads this to<br/>stamp every build</u>") --> V
    click Bin href "env.sh.md"
    click Rel href "release.sh.md"
    class Bin hexNode
    class V,Shell,Rel shFile
    class Json jsonFile
    classDef hexNode fill:#ffe08a,stroke:#a16207,color:#422006
    classDef shFile fill:#abe07a,stroke:#3f6212,color:#16280a
    classDef jsonFile fill:#cdb9f7,stroke:#6d28d9,color:#2e1065
```

`scripts/version.sh` prints the version of the repository as computed by `tools/versioner/update.sh.md`, so the version of a build and the version of a release can never drift apart. It is sourced-independent: it loads [entry.sh.md](entry.sh.md) itself and works from any cwd.

## Usage

```bash
scripts/version.sh                 # VERSION=<v> and TAG=<t> lines (default)
scripts/version.sh --format text   # same as default
scripts/version.sh --format shell  # export VERSION=... and export TAG=...
scripts/version.sh --format json   # raw json from versioner
```

Unknown arguments and unknown `--format` values exit 2; a missing/unexecutable `VERSIONER_BIN` exits 1 with instructions to run `tools/versioner/update.sh`.

## How it resolves the binary

`VERSIONER_BIN` (via the [env cascade](env.sh.md)) or `VERSIONER_REPO_DIR` (defaults to the repository root) are the knobs. The `json` format delegates to `versioner --quiet --format json`; the `text`/`shell` formats parse the `VERSION=`/`TAG=` lines from its output and fail with exit 1 if either cannot be parsed. `.versioner.json` at the repo root configures the five-part version scheme (see "Versioning" in [../README.md](../README.md)).

## Related pages

- [env.sh.md](env.sh.md) — where `VERSIONER_BIN` comes from
- `tools/versioner/update.sh.md` — the bundled binary and how to refresh it
- [release.sh.md](release.sh.md) — releases using the same computation
- [README.md](README.md)
