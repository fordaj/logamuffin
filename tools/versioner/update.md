# The versioner bundle (`tools/versioner/`)

[Versioner](https://github.com/fordaj/Versioner) derives a five-part version (`branch.major.minor.revision.patch`) from git history. This repository uses it to stamp every build: `scripts/build.sh`, `run_tests.sh`, `tools/generate_version.sh`, and `scripts/release.sh` all call the bundled binary so `--version` always reflects `HEAD`. `.versioner.json` configures the scheme (main exempt to branch-version `0`, other branches to `254`). See "Versioning" in [../../README.md](../../README.md) for the full story (`.version/version.hpp`, `LOGAMUFFIN_VERSION`, `LOGAMUFFIN_VERSION_TAG`).

## Layout

```text
tools/versioner/
  update.sh                     # refresh from fordaj/Versioner releases
  v0.0.3.0.5/
    macos-arm64/versioner
    macos-x86_64/versioner
    linux-arm64/versioner
    linux-x86_64/versioner
    windows-x86_64/versioner.exe
```

Note: no `windows-arm64` bundle exists upstream — `scripts/.windows-arm64.env` points `VERSIONER_BIN` at the `windows-x86_64` build and documents overriding it with a versioner on `PATH`.

Pinned as `VERSIONER_VERSION_DIR=tools/versioner/v0.0.3.0.5` in `scripts/.common.env`; selected via `VERSIONER_BIN` in the platform env files (see [../../scripts/env.md](../../scripts/env.md)).

## `update.sh` in detail

Assets are `Versioner-<platform>.tar.gz` (macOS/Linux) and `Versioner-<platform>.zip` (Windows). The script:

1. Resolves the tag (latest release, or `--tag vX.Y.Z...`), validating the tag shape.
2. Downloads via `gh`, then verifies the downloaded archives against upstream `checksums.txt`/`SHA256SUMS.txt` (`shasum -a 256 -c` or `sha256sum -c`), looking up each asset by name. A missing checksums file only warns; a mismatch is fatal.
3. Extracts each archive (`tar` for `.tar.gz`; `unzip` for `.zip`; `7z`/`7zz` fallback for legacy `.7z`), copies the binary into `v<tag>/<platform>/`, and makes it executable. Only the binary is kept.
4. Swaps in the new version directory and deletes superseded ones, then smoke-tests the host binary (`versioner --version`).

Flags: `--dry-run`, `--tag TAG`, `--repo REPO`. Environment overrides: `VERSIONER_REPO` (default `fordaj/Versioner`), `VERSIONER_TAG`.

## Related pages

- [../README.md](../README.md) — the bundle index
- [../generate_version.md](../generate_version.md) — what consumes this binary to write `.version/version.hpp`
- [../../scripts/version.md](../../scripts/version.md) — read-only version printing
- [../../scripts/release.md](../../scripts/release.md) — releases tagged with this version
