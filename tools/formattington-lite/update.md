# The formattington-lite bundle (`tools/formattington-lite/`)

[Formattington](https://github.com/fordaj/formattington) is the formatter orchestrator (prettier, clang-format, shfmt and friends) used by this repository's `format.sh`/`ci/format_check.sh` — via the `pyproject.toml`/`uv.lock` Python pin, not this bundle. The `formattington-lite` bundle is a slim prebuilt binary kept in the repo for direct invocation; it is **not** wired into the environment cascade (`scripts/.common.env` pins only versioner, logamuffin, runner-spawner, and barnacle).

## Layout

```text
tools/formattington-lite/
  update.sh                     # refresh from fordaj/formattington releases
  v0.0.2.0.53/
    macos-arm64/formattington
    macos-x86_64/formattington
    linux-arm64/formattington
    linux-x86_64/formattington
    windows-arm64/formattington.exe
    windows-x86_64/formattington.exe
```

The bundle is pinned by its version directory (`tools/formattington-lite/v0.0.2.0.53/`); no `*_VERSION_DIR` in `scripts/.common.env` references it.

## `update.sh` in detail

Same shape as the other bundles in this directory, with formattington specifics:

1. Resolves the tag (latest release, or `--tag vX.Y.Z...`), validating the tag shape.
2. Downloads only `-lite` archives via `gh` (assets without a `-lite` suffix are ignored, unlike the plain bundles), then verifies against upstream `checksums.txt`/`SHA256SUMS.txt` — a missing checksums file only warns; a mismatch is fatal.
3. Extracts each archive (`tar` for `.tar.gz`; `unzip` for `.zip`; `7z`/`7zz` fallback for legacy `.7z`), copies the binary into `v<tag>/<platform>/`, and makes it executable. `NOTICE.md` and `LICENSES/`, if present, are copied alongside the binary into the per-platform directory.
4. Swaps in the new version directory and deletes superseded ones, then smoke-tests the host binary (`formattington --version`).

Flags: `--dry-run`, `--tag TAG`, `--repo REPO`. Environment overrides: `FORMATTINGTON_REPO` (default `fordaj/formattington`), `FORMATTINGTON_TAG`. Note the script's bundle/usage text says "formattington" for binaries while the bundle directory it manages is `tools/formattington-lite/`.

## Related pages

- [../README.md](../README.md) — the bundle index
- [../../format.md](../../format.md) — the formatting workflow (pinned via uv, not this bundle)
- [../../scripts/README.md](../../scripts/README.md)
