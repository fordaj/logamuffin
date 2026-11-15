# The logamuffin bundle (`tools/logamuffin/`)

[Logamuffin](https://github.com/fordaj/logamuffin) is the structured logging backend: a small C++20 binary used by this repository's tooling via `scripts/entry.sh`'s `log()` (and by the fordaj ecosystem more broadly — this repository is logamuffin's source). This bundle is the prebuilt upstream binary, distinct from the `logamuffin` built from this tree.

## Layout

```text
tools/logamuffin/
  update.bash                     # refresh from fordaj/logamuffin releases
  v0.1.3.0.35/                    # NOT committed to git at present
    macos-arm64/logamuffin        # (fetch with update.bash)
    ...
```

`scripts/.common.env` pins `LOGAMUFFIN_VERSION_DIR=tools/logamuffin/v0.1.3.0.35` and the platform env files point `LOGAMUFFIN_BIN` at it. Only `update.bash` is tracked in git — the v0.1.3.0.35 binaries are absent from this checkout, so `scripts/entry.sh`'s `log()` currently falls back to printf until the bundle is fetched. (The `.gitignore` does not exclude them; they simply have not been committed/fetched here.)

Pinned as `LOGAMUFFIN v0.1.3.0.35`; see [../../scripts/env.md](../../scripts/env.md).

## `update.bash` in detail

Assets are `logamuffin-<platform>.tar.gz` (macOS/Linux) and `logamuffin-<platform>.zip` (Windows), carrying the binary at the archive root or inside a single top-level directory. The script:

1. Resolves the tag (latest release, or `--tag vX.Y.Z...`), validating the tag shape.
2. Downloads via `gh`, then verifies the downloaded archives against upstream `checksums.txt`/`SHA256SUMS.txt` (`shasum -a 256 -c` or `sha256sum -c`), looking up each asset by name. A missing checksums file only warns; a mismatch is fatal.
3. Extracts each archive (`tar` for `.tar.gz`; `unzip` for `.zip`; `7z`/`7zz` fallback for legacy `.7z`), copies the binary into `v<tag>/<platform>/`, and makes it executable. Only the binary is kept (no LICENSE copy).
4. Swaps in the new version directory and deletes superseded ones, then smoke-tests the host binary (`logamuffin --version`).

Flags: `--dry-run`, `--tag TAG`, `--repo REPO`. Environment overrides: `LOGAMUFFIN_REPO` (default `fordaj/logamuffin`), `LOGAMUFFIN_TAG`.

## How the repository uses it

`log()` in [../../scripts/entry.sh](../../scripts/entry.md) forwards to `$LOGAMUFFIN_BIN` when it is executable; `log.sh` at the repo root (the user-facing Bash helper, see [../../log.md](../../log.md)) resolves `LOGAMUFFIN_BIN` or falls back to PATH/build outputs.

## Related pages

- [../README.md](../README.md) — the bundle index
- [../../scripts/entry.md](../../scripts/entry.md) — the `log()` that consumes this binary
- [../../scripts/env.md](../../scripts/env.md) — cascade and pins
