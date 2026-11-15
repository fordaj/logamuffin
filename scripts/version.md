# Printing the repository version (`scripts/version.bash`)

`scripts/version.bash` prints the version of the repository as computed by [Versioner](../tools/versioner/update.md), so the version of a build and the version of a release can never drift apart. It is sourced-independent: it loads [entry.md](entry.md) itself and works from any cwd.

## Usage

```bash
scripts/version.bash                 # VERSION=<v> and TAG=<t> lines (default)
scripts/version.bash --format text   # same as default
scripts/version.bash --format shell  # export VERSION=... and export TAG=...
scripts/version.bash --format json   # raw json from versioner
```

Unknown arguments and unknown `--format` values exit 2; a missing/unexecutable `VERSIONER_BIN` exits 1 with instructions to run `tools/versioner/update.bash`.

## How it resolves the binary

`VERSIONER_BIN` (via the [env cascade](env.md)) or `VERSIONER_REPO_DIR` (defaults to the repository root) are the knobs. The `json` format delegates to `versioner --quiet --format json`; the `text`/`shell` formats parse the `VERSION=`/`TAG=` lines from its output and fail with exit 1 if either cannot be parsed. `.versioner.json` at the repo root configures the five-part version scheme (see "Versioning" in [../README.md](../README.md)).

## Related pages

- [env.md](env.md) — where `VERSIONER_BIN` comes from
- [../tools/versioner/update.md](../tools/versioner/update.md) — the bundled binary and how to refresh it
- [release.md](release.md) — releases using the same computation
- [README.md](README.md)
